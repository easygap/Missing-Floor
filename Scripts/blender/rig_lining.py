"""덧댄 것(SK_Lining)을 짓고 뼈를 단다.

벌어진 벽을 몸으로 덮는 괴이다. 은폐 공사에 쓰고 남은 재료로 되어 있다. 몸은 낮고 넓은 한
덩어리다. 누렇게 산화한 계란판 방음재 여러 장을 눌러 뭉친 것을 검은 아스팔트 방수막으로 겹겹이
감았고, 막의 이음매마다 굳은 실리콘 줄이 지나간다. 등에는 찌그러진 아연도금 ㄷ자 철골이
등뼈처럼 반쯤 묻혀 있다. 바닥(누르는 면)에는 실리콘 테두리가 둘려 있어 벽에 대면 틈을 막는다.
다리는 넷이고 마디마다 볼트로 접히는 접자처럼 ㄷ자 철골 세 마디다. 철골은 손으로 아무렇게나
감은 방수막에 거의 묻혔다. 한쪽은 막이 뭉쳐 두껍고 반대쪽은 얇게 당겨졌으며, 찢긴 틈으로
쑤셔 넣은 방음재가 비어져 나오고 풀린 막 끝이 들려 있다. 마디의 볼트와 철골 끝만 드러난다.
몸 옆구리에서 윗마디까지 막이 물갈퀴처럼 이어져 몸과 다리가 한 몸으로 읽힌다. 끝(패드)은
뜯어 뭉친 방음재 덩어리다. 계란판 면이 그대로 드러나 있고 한쪽이 두꺼우며, 찢긴 막 조각이 몸
쪽 절반을 덮고 끊긴 실리콘 줄 토막이 둘레에 붙어 있다. 눈·입·머리·사람 손발은 없다.

몸통은 생성 모델이다. 시안 `Content/SourceArt/Generated/LiningBody/source-body.png`(몸통만,
다리 붙는 자리 넷)를 TRELLIS.2로 뽑은 `trellis1024-s56/raw/pbr_00001_.glb`를 읽어 몸 치수
(80 × 120 × 40 cm)에 맞추고, 고밀도에서 색·노멀·거칠기를 굽는다. 다리·물갈퀴·패드·실리콘
테두리는 여기서 절차로 짓는다. 다리와 패드 꼴은 `Content/SourceArt/AI/LiningLegPadSheet_20261019.png`,
걷는 순간(배가 처지고 패드보다 막이 먼저 벌어짐)은 `LiningStepInSitu_20261019.png`를 따른다.
전체 모습 시안은 `LiningTurnaround_20261015.png`, 처음 시안은 `LiningModelSheet_20261013.png`·
`LiningInSitu_20261013.png`다.

바인드 포즈는 몸을 바닥에 눕히고 다리 넷을 대각선으로 편 모습이다. 동작은 굽지 않는다.
게임(AIGLiningEntity)이 UPoseableMeshComponent로 뼈를 직접 놓는다. 접힌 대기, 패드가 벽에서
떨어지는 순간, 두 다리로 짚고 몸을 당기는 걸음, 천장까지 편 몸, 틀에 걸린 관절이 전부 같은
뼈의 자세다. 몸이 처지는 것과 막이 벌어지는 것도 게임이 뼈로 놓는다.

    blender -b --factory-startup --python Scripts/blender/rig_lining.py

뼈(UE 좌표, 미터): root(원점) → body(몸 가운데 위) → leg_{fl,fr,bl,br}_1(엉덩이→무릎) →
_2(무릎→발목) → _3(발목→패드 가운데). f는 앞(+X), b는 뒤, l은 왼쪽(-Y), r은 오른쪽(+Y).
철골·볼트·엉덩이 받침은 뼈 하나에 통째로 붙는다(철골은 휘지 않는다). 무른 데는 뼈를 더 둔다.
- mass_{fl,…}(body 아래): 몸 사분면의 살. 등뼈 철골과 엉덩이 받침 둘레는 body에 남고, 배와
  옆구리가 이 뼈를 따라 처지고 늘어진다.
- web_{fl,…}(body 아래): 물갈퀴 가운데. 게임이 몸과 윗마디 사이에 두고 처지거나 먼저 벌어지게 민다.
- skin_{fl,…}(leg_*_3 아래): 패드 막 조각의 몸 쪽 끝. 패드가 떨어질 때 벽에 남아 늘어났다가 놓인다.
"""

import argparse
import json
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import bpy  # noqa: E402
import bmesh  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402
from mathutils.bvhtree import BVHTree  # noqa: E402

import ig_blender_lib as ig  # noqa: E402

SOURCE_ART = os.path.abspath(os.path.join(HERE, "..", "..", "Content", "SourceArt"))
SHELL_GLB = os.path.join(SOURCE_ART, "Generated", "LiningBody", "trellis1024-s56", "raw", "pbr_00001_.glb")

# 몸(접혔을 때 벽에 기대면 높이 0.8 m, 폭 1.2 m가 된다). 게임의 IGLining::BodyHalf와 같은 상자다.
BODY_X = 0.80
BODY_Y = 1.20
BODY_Z = 0.40
# 생성 몸통의 긴 축은 원본에서 -8.7도였다. 몸의 긴 축(Y)으로 돌린다.
SHELL_YAW = 98.7
SHELL_TRIS = 17000
# 엉덩이 볼트 자리.
HIP_X = 0.33
HIP_Y = 0.52
HIP_Z = 0.22
# 다리 마디 길이. 다 펴면 엉덩이에서 패드까지 약 1.7 m, 대각선으로 몸 전체가 4.6 m쯤 걸친다.
SEG1 = 0.80
SEG2 = 0.78
SEG3_OUT = 0.10
SEG3_DOWN = 0.16
LEGS = {"fl": (1.0, -1.0), "fr": (1.0, 1.0), "bl": (-1.0, -1.0), "br": (-1.0, 1.0)}
# 몸 처짐 뼈 자리(사분면 가운데). 등뼈에서 멀수록, 엉덩이 받침에서 멀수록 이 뼈를 따른다.
MASS_X = 0.20
MASS_Y = 0.30
MASS_Z = 0.14
# 물갈퀴 가운데와 패드 막 끝 자리. build_parts가 몸통 겉면을 보고 채운다(뼈를 놓을 때 쓴다).
WEB_ANCHORS = {}
SKIN_ANCHORS = {}
# 충돌 모양과 맞춰 볼 치수(build_parts가 모은다).
COLLISION_REF = {}


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", default="SK_Lining")
    parser.add_argument("--texture-size", type=int, default=2048)
    parser.add_argument("--out", default=None)
    parser.add_argument("--shell-glb", default=SHELL_GLB)
    parser.add_argument("--shell-tris", type=int, default=SHELL_TRIS)
    parser.add_argument("--no-bake", action="store_true")
    return parser.parse_args(argv)


# --------------------------------------------------------------------------
# 재질
# --------------------------------------------------------------------------

def tiled_material(name, stem, tile_m, metallic=0.0, normal_strength=1.0, rough_scale=1.0, contact_grime=0.0, tint=None):
    """저장소 타일(D·N·R)을 ImageUV 층으로 반복해 읽는다. _N은 DirectX라 G를 뒤집어 넣는다.
    contact_grime이 있으면 아래를 향한 면(바인드 자세에서 벽·바닥을 누르는 면)을 회갈색 때로 덮는다."""
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

    def socket(sockets, name):
        # 섞기 노드는 자료형마다 같은 이름의 소켓을 따로 둔다. 색 소켓을 고른다.
        return next(item for item in sockets if item.name == name and item.type == "RGBA")

    color = image("D", "sRGB").outputs["Color"]
    if tint is not None:
        # 타일 색을 곱해 어둡게 한다(검은 아스팔트 막, 때 묻은 아연도금).
        tinted = nodes.new("ShaderNodeMix")
        tinted.data_type = "RGBA"
        tinted.blend_type = "MULTIPLY"
        next(i for i in tinted.inputs if i.name == "Factor" and i.type == "VALUE").default_value = 1.0
        links.new(color, socket(tinted.inputs, "A"))
        socket(tinted.inputs, "B").default_value = (*tint, 1.0)
        color = socket(tinted.outputs, "Result")
    if contact_grime > 0.0:
        # 바인드 자세에서 몸과 패드의 바닥이 누르는 면이다. 법선이 아래를 볼수록 때가 진하다.
        geometry = nodes.new("ShaderNodeNewGeometry")
        split = nodes.new("ShaderNodeSeparateXYZ")
        links.new(geometry.outputs["Normal"], split.inputs["Vector"])
        ramp = nodes.new("ShaderNodeMapRange")
        ramp.inputs["From Min"].default_value = -0.25
        ramp.inputs["From Max"].default_value = -0.95
        ramp.inputs["To Min"].default_value = 0.0
        ramp.inputs["To Max"].default_value = contact_grime
        ramp.clamp = True
        links.new(split.outputs["Z"], ramp.inputs["Value"])
        mix = nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        mix.blend_type = "MULTIPLY"
        socket(mix.inputs, "B").default_value = (0.42, 0.39, 0.35, 1.0)
        links.new(ramp.outputs["Result"], next(item for item in mix.inputs if item.name == "Factor" and item.type == "VALUE"))
        links.new(color, socket(mix.inputs, "A"))
        links.new(socket(mix.outputs, "Result"), bsdf.inputs["Base Color"])
    else:
        links.new(color, bsdf.inputs["Base Color"])
    rough = image("R", "Non-Color")
    if rough_scale != 1.0:
        mul = nodes.new("ShaderNodeMath")
        mul.operation = "MULTIPLY"
        mul.inputs[1].default_value = rough_scale
        mul.use_clamp = True
        links.new(rough.outputs["Color"], mul.inputs[0])
        links.new(mul.outputs["Value"], bsdf.inputs["Roughness"])
    else:
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


def flat_material(name, color, roughness, metallic=0.0):
    mat = bpy.data.materials.new(name)
    bsdf = ig._principled(mat)
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    mat["ig_kind"] = "baked"
    return mat


# --------------------------------------------------------------------------
# 조각(전부 월드 좌표로 바로 짓는다. 오브젝트 변환은 항등이다)
# --------------------------------------------------------------------------

def _finish(name, bm, material, bone):
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = ig._link(bpy.data.objects.new(name, me))
    ig.assign_material(ob, material)
    triplanar_uv(ob)
    group = ob.vertex_groups.new(name=bone)
    group.add(list(range(len(me.vertices))), 1.0, "REPLACE")
    return ob


def _finish_keep_uv(name, bm, material, bone):
    """_finish와 같지만 bmesh에 넣어 둔 ImageUV를 그대로 쓰고 매끈하게 음영을 준다(둥근 자루는 축
    투영과 평면 음영이 마디마다 끊겨 깍두기처럼 보였다)."""
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    for face in bm.faces:
        face.smooth = True
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = ig._link(bpy.data.objects.new(name, me))
    ig.assign_material(ob, material)
    group = ob.vertex_groups.new(name=bone)
    group.add(list(range(len(me.vertices))), 1.0, "REPLACE")
    return ob


def triplanar_uv(ob):
    """면 노멀이 가장 가까운 축으로 월드 좌표를 투영한 타일 UV(ImageUV)."""
    me = ob.data
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


def oriented_box(name, center, size, axes, material, bone, bevel=0.0, segments=2):
    """axes=(x축, y축, z축) 단위 벡터. size는 그 축 방향 치수."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    ax, ay, az = (Vector(a).normalized() for a in axes)
    rot = Matrix((ax, ay, az)).transposed()
    for v in bm.verts:
        local = Vector((v.co.x * size[0], v.co.y * size[1], v.co.z * size[2]))
        v.co = Vector(center) + rot @ local
    if bevel > 0.0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=segments, affect="EDGES", profile=0.5)
    return _finish(name, bm, material, bone)


def frame_from(axis, up_hint):
    x = Vector(axis).normalized()
    up = Vector(up_hint)
    up = (up - x * up.dot(x))
    if up.length < 1e-4:
        up = Vector((0.0, 0.0, 1.0)) if abs(x.z) < 0.9 else Vector((1.0, 0.0, 0.0))
        up = (up - x * up.dot(x))
    z = up.normalized()
    y = z.cross(x).normalized()
    return x, y, z


def c_channel(name, a, b, up_hint, width, flange, thick, material, bone):
    """a→b로 뻗은 ㄷ자 철골. 플랜지는 up_hint 쪽으로 열린다."""
    a = Vector(a)
    b = Vector(b)
    x, y, z = frame_from(b - a, up_hint)
    hw = width * 0.5
    # 단면(y, z): 등판과 양쪽 플랜지.
    outline = [(-hw, 0.0), (hw, 0.0), (hw, flange), (hw - thick, flange), (hw - thick, thick),
               (-hw + thick, thick), (-hw + thick, flange), (-hw, flange)]
    bm = bmesh.new()

    def ring(origin):
        return [bm.verts.new(origin + y * py + z * pz) for py, pz in outline]

    r0 = ring(a)
    r1 = ring(b)
    count = len(outline)
    for i in range(count):
        j = (i + 1) % count
        bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
    # 마구리는 직사각형 셋(등판·플랜지 둘)으로 덮는다.
    for ring_verts, flip in ((r0, True), (r1, False)):
        quads = [(0, 1, 4, 5), (5, 6, 7, 0), (1, 2, 3, 4)]
        for quad in quads:
            verts = [ring_verts[k] for k in quad]
            if flip:
                verts.reverse()
            try:
                bm.faces.new(verts)
            except ValueError:
                pass
    return _finish(name, bm, material, bone)


def bolt(name, center, axis, radius, length, material, bone):
    center = Vector(center)
    x, y, z = frame_from(axis, (0.0, 0.0, 1.0))
    bm = bmesh.new()
    segments = 12
    rings = []
    for t in (-length * 0.5, length * 0.5):
        rings.append([bm.verts.new(center + x * t + (y * math.cos(a) + z * math.sin(a)) * radius)
                      for a in (math.tau * i / segments for i in range(segments))])
    for i in range(segments):
        j = (i + 1) % segments
        bm.faces.new((rings[0][i], rings[0][j], rings[1][j], rings[1][i]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[1])
    # 육각 너트 둘.
    for t in (-length * 0.5, length * 0.5):
        hexes = []
        for d in (-0.008, 0.008):
            hexes.append([bm.verts.new(center + x * (t + d) + (y * math.cos(a) + z * math.sin(a)) * radius * 1.8)
                          for a in (math.tau * i / 6 + math.pi / 6 for i in range(6))])
        for i in range(6):
            j = (i + 1) % 6
            bm.faces.new((hexes[0][i], hexes[0][j], hexes[1][j], hexes[1][i]))
        bm.faces.new(list(reversed(hexes[0])))
        bm.faces.new(hexes[1])
    return _finish(name, bm, material, bone)


def _blend_two(ob, bones, weight_of):
    """정점마다 weight_of(co)를 두 번째 뼈의 무게로 주고 나머지를 첫 뼈에 준다."""
    a = ob.vertex_groups.new(name=bones[0])
    b = ob.vertex_groups.new(name=bones[1])
    for v in ob.data.vertices:
        w = max(0.0, min(1.0, weight_of(v.co)))
        a.add([v.index], 1.0 - w, "REPLACE")
        b.add([v.index], w, "REPLACE")


def _assign_weights(ob, weight_of):
    """정점마다 weight_of(co)가 돌려준 {뼈: 무게}를 합이 1이 되게 나눠 건다."""
    groups = {}
    for v in ob.data.vertices:
        weights = {bone: w for bone, w in weight_of(v.co).items() if w > 1e-4}
        total = sum(weights.values())
        for bone, w in weights.items():
            if bone not in groups:
                groups[bone] = ob.vertex_groups.get(bone) or ob.vertex_groups.new(name=bone)
            groups[bone].add([v.index], w / total, "REPLACE")


def _smooth(x):
    t = max(0.0, min(1.0, x))
    return t * t * (3.0 - 2.0 * t)


def _ang_diff(a, b):
    return (a - b + math.pi) % math.tau - math.pi


def _turn(v, radians):
    """수평 벡터를 Z축으로 돌린다."""
    c, s = math.cos(radians), math.sin(radians)
    return Vector((v.x * c - v.y * s, v.x * s + v.y * c, v.z))


def mass_weights(co):
    """몸통 살의 무게. 등뼈 철골(위 가운데)과 엉덩이 받침 둘레는 몸 뼈에 남고, 거기서 멀어질수록
    사분면 처짐 뼈 넷이 나눠 가진다. 가운데에서는 사분면끼리 매끄럽게 섞인다."""
    spine = math.hypot(co.x, max(0.0, 0.34 - co.z))
    hip = min((co - Vector((HIP_X * sx, HIP_Y * sy, HIP_Z))).length for sx, sy in LEGS.values())
    rigid = max(1.0 - _smooth((spine - 0.05) / 0.22), 1.0 - _smooth((hip - 0.07) / 0.15))
    fx = _smooth(0.5 + co.x / (2.0 * 0.30))
    fy = _smooth(0.5 + co.y / (2.0 * 0.45))
    out = {"body": rigid}
    for key, (sx, sy) in LEGS.items():
        out[f"mass_{key}"] = (1.0 - rigid) * (fx if sx > 0 else 1.0 - fx) * (fy if sy > 0 else 1.0 - fy)
    return out


def strand(name, p0, p1, sag, width, material, bones, steps=8):
    """두 점 사이에 처진 접착층 띠. 가운데가 좁아지고 아래로 처진다.
    웨이트는 길이를 따라 두 뼈를 섞어 다리를 펴면 띠가 늘어난다."""
    p0 = Vector(p0)
    p1 = Vector(p1)
    axis_dir = (p1 - p0).normalized()
    across = axis_dir.cross(Vector((0.0, 0.0, 1.0)))
    if across.length < 1e-4:
        across = Vector((1.0, 0.0, 0.0))
    across.normalize()
    up = across.cross(axis_dir).normalized()
    bm = bmesh.new()
    rows = []
    for i in range(steps + 1):
        t = i / steps
        p = p0.lerp(p1, t)
        p.z -= sag * 4.0 * t * (1.0 - t)
        half = width * (0.5 - 0.32 * math.sin(math.pi * t))
        thick = 0.003
        rows.append([bm.verts.new(p + across * sx * half + up * sz * thick) for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1))])
    for i in range(steps):
        a_row, b_row = rows[i], rows[i + 1]
        for k in range(4):
            m = (k + 1) % 4
            bm.faces.new((a_row[k], a_row[m], b_row[m], b_row[k]))
    bm.faces.new(list(reversed(rows[0])))
    bm.faces.new(rows[-1])
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    for face in bm.faces:
        face.smooth = True
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = ig._link(bpy.data.objects.new(name, me))
    ig.assign_material(ob, material)
    triplanar_uv(ob)
    axis = p1 - p0
    length2 = max(axis.length_squared, 1e-6)

    def weight(co):
        t = max(0.0, min(1.0, (co - p0).dot(axis) / length2))
        return t * t * (3.0 - 2.0 * t)

    _blend_two(ob, bones, weight)
    return ob


def bead(name, points, radius, material, bone):
    """짜 놓은 실리콘 줄. bone이 None이면 웨이트를 걸지 않는다(부르는 쪽이 건다)."""
    ob = ig.pipe(name, [Vector(p) for p in points], radius, resolution=6)
    ig.assign_material(ob, material)
    triplanar_uv(ob)
    if bone:
        group = ob.vertex_groups.new(name=bone)
        group.add(list(range(len(ob.data.vertices))), 1.0, "REPLACE")
    return ob


def _wobble(seed, a, b=0.0):
    """정해진 씨앗으로 매끈하게 흔들리는 값(-1..1). 실행마다 같은 모양이 나온다."""
    return (math.sin(a * 2.3 + seed * 1.7 + b * 0.9) * 0.6 + math.sin(a * 5.1 + seed * 3.1 - b * 2.2) * 0.3
            + math.sin(a * 9.7 + seed * 0.7 + b * 4.3) * 0.1)


def _sheet(name, grid, material, thickness):
    """점 격자(열마다 같은 수)를 판으로 잇고 두께를 준 막 조각. 정점은 웨이트 없이 돌려준다."""
    bm = bmesh.new()
    verts = [[bm.verts.new(p) for p in row] for row in grid]
    for i in range(len(verts) - 1):
        for j in range(len(verts[i]) - 1):
            bm.faces.new((verts[i][j], verts[i + 1][j], verts[i + 1][j + 1], verts[i][j + 1]))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = ig._link(bpy.data.objects.new(name, me))
    mod = ob.modifiers.new("Solidify", "SOLIDIFY")
    mod.thickness = thickness
    mod.offset = 0.0
    ig.apply_modifiers(ob)
    for poly in ob.data.polygons:
        poly.use_smooth = True
    ig.assign_material(ob, material)
    triplanar_uv(ob)
    return ob


class WrapShape:
    """철골 마디를 손으로 감은 방수막 자루의 겉모양. radius(t, ang)은 자루 축에서 겉면까지(m)다.
    t는 a→b 0..1, ang은 단면의 y축에서 z축 쪽으로 잰 각이다. 한쪽(bulge_ang 둘레)은 막이 뭉쳐 두껍고
    반대쪽은 얇게 당겨졌다. 막은 구겨져 비스듬한 접힘 선이 여기저기 서고, 감아 겹친 막의 턱이 나선으로
    두 줄 지나가며, 양 끝은 마디 쪽으로 고르지 않게 오므라든다. 매끈한 혹만 두면 근육이나 기계 팔처럼
    읽혀서 접힘 선이 실루엣과 반사를 잘게 끊게 했다. ㄷ자 철골(플랜지가 up_hint 쪽)의 가운데에 맞춰
    offset_z만큼 띄운다."""

    def __init__(self, a, b, up_hint, ry, rz, offset_z, seed, bulge, folds=20):
        self.a = Vector(a)
        self.b = Vector(b)
        self.x, self.y, self.z = frame_from(self.b - self.a, up_hint)
        self.length = (self.b - self.a).length
        self.ry, self.rz, self.offset_z = ry, rz, offset_z
        self.seed = seed
        self.girth = math.pi * (ry + rz)
        rnd = random.Random(seed)
        self.bulge_ang = rnd.uniform(0.0, math.tau)
        # 뭉친 쪽: (길이 자리, 둘레 자리, 길이 폭 m, 둘레 폭 rad, 높이 m)
        self.lumps = [(rnd.uniform(0.12, 0.88), self.bulge_ang + rnd.uniform(-1.0, 1.0), rnd.uniform(0.07, 0.16),
                       rnd.uniform(0.5, 0.95), bulge * rnd.uniform(0.5, 1.0)) for _ in range(5)]
        # 접힘 선: (길이 자리 m, 둘레 자리 m, 선 방향 cos·sin, 길이 m, 높이 m(음수면 골), 반폭 m)
        self.folds = []
        for _ in range(folds):
            beta = rnd.choice((-1.0, 1.0)) * rnd.uniform(0.45, 1.35)
            self.folds.append((rnd.uniform(0.0, self.length), rnd.uniform(0.0, self.girth), math.cos(beta), math.sin(beta),
                               rnd.uniform(0.07, 0.24), rnd.uniform(0.007, 0.016) * rnd.choice((1.0, 1.0, -0.7)),
                               rnd.uniform(0.012, 0.030)))
        # 밀려 뭉친 띠: 소매를 밀어 올린 것처럼 한쪽으로 쏠려 두꺼워진 자리(길이 자리, 쏠린 쪽, 높이 m).
        self.bunches = [(rnd.uniform(0.15, 0.85), rnd.uniform(0.0, math.tau), rnd.uniform(0.008, 0.016)) for _ in range(3)]
        # 겹친 막의 턱: (나선 한 바퀴에 나가는 길이 m, 부호가 감은 방향. 위상. 턱 높이 m)
        self.ridges = [(rnd.uniform(0.15, 0.27) * rnd.choice((-1.0, 1.0)), rnd.random(), rnd.uniform(0.005, 0.008))
                       for _ in range(2)]
        self.end_a = rnd.uniform(0.05, 0.11)
        self.end_b = rnd.uniform(0.05, 0.10)

    def radius(self, t, ang):
        c, s = math.cos(ang), math.sin(ang)
        r = (self.ry * self.rz) / math.sqrt((self.rz * c) ** 2 + (self.ry * s) ** 2)
        end = _smooth(t / self.end_a) * _smooth((1.0 - t) / self.end_b)
        r *= 0.80 + 0.20 * end
        for tj, aj, span, width, height in self.lumps:
            dt = (t - tj) * self.length / span
            da = _ang_diff(ang, aj) / width
            r += height * math.exp(-(dt * dt + da * da)) * end
        for tb, ab, height in self.bunches:
            db = (t - tb) * self.length / 0.028
            r += height * math.exp(-db * db) * (0.55 + 0.45 * math.cos(ang - ab)) * end
        along = t * self.length
        around = (ang % math.tau) / math.tau * self.girth
        for s0, p0, cb, sb, span, height, half in self.folds:
            ds = along - s0
            dp = (around - p0 + self.girth * 0.5) % self.girth - self.girth * 0.5
            u = ds * cb + dp * sb
            v = abs(-ds * sb + dp * cb)
            if v < half and abs(u) < span * 0.5:
                # 단면이 세모인 날카로운 접힘. 선 끝으로 갈수록 잦아든다.
                r += height * (1.0 - v / half) * (1.0 - (2.0 * u / span) ** 2) * (0.4 + 0.6 * end)
        for pitch, phase, step in self.ridges:
            u = along / pitch + ang / math.tau + phase
            f = u - math.floor(u)
            # 한 바퀴 동안 천천히 두꺼워지다 감은 끝에서 뚝 떨어진다.
            r += step * (_smooth(f / 0.85) if f < 0.85 else 1.0 - (f - 0.85) / 0.15)
        return r * (1.0 + 0.04 * _wobble(self.seed + 7, t * 7.0, ang * 2.0))

    def direction(self, ang):
        return self.y * math.cos(ang) + self.z * math.sin(ang)

    def point(self, t, ang, lift=0.0):
        origin = self.a + self.x * (self.length * t) + self.z * self.offset_z
        return origin + self.direction(ang) * (self.radius(t, ang) + lift)


def wrap(name, shape, material, bone, sides=22, step=0.022):
    """WrapShape를 따라 감은 막 자루. 둘레·길이를 실제 m로 편 UV(ImageUV)를 쓰고, 접힘 선은 날을 세운다."""
    rings = max(6, int(round(shape.length / step)))
    bm = bmesh.new()
    loops = [[bm.verts.new(shape.point(i / rings, math.tau * k / sides)) for k in range(sides)] for i in range(rings + 1)]
    uv_layer = bm.loops.layers.uv.new("ImageUV")
    girth = math.pi * (shape.ry + shape.rz)
    for i in range(rings):
        for k in range(sides):
            m = (k + 1) % sides
            face = bm.faces.new((loops[i][k], loops[i][m], loops[i + 1][m], loops[i + 1][k]))
            for loop, (ku, iv) in zip(face.loops, ((k, i), (k + 1, i), (k + 1, i + 1), (k, i + 1))):
                loop[uv_layer].uv = (girth * ku / sides, shape.length * iv / rings)
    for cap in (list(reversed(loops[0])), loops[-1]):
        face = bm.faces.new(cap)
        for loop in face.loops:
            loop[uv_layer].uv = (loop.vert.co.x * 0.5, loop.vert.co.y * 0.5)
    ob = _finish_keep_uv(name, bm, material, bone)
    ig.mark_sharp_by_angle(ob, 40.0)
    return ob


def foam_chunk(name, center, axes, half, material, bone, seed, plane=None, lop=0.35, ragged=0.12, segs=18, rings=9):
    """뜯어 뭉친 방음재 덩어리. 납작한 타원체를 바탕으로 둘레를 들쭉날쭉하게 뜯고 한쪽을 두껍게 했다.
    plane=(점, 법선)이 있으면 그 아래를 눌러 평평한 짚는 면으로 만든다. 계란판 요철은 타일이 맡는다."""
    ax, ay, az = (Vector(v).normalized() for v in axes)
    center = Vector(center)
    lop_ang = random.Random(seed).uniform(0.0, math.tau)
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=segs, v_segments=rings, radius=1.0)
    for v in bm.verts:
        x, y, z = v.co
        ang = math.atan2(y, x)
        ring = math.sqrt(x * x + y * y)
        # 뜯긴 둘레: 각마다 반지름이 들쭉날쭉하고, 가장자리일수록 더 부스러졌다.
        edge = 1.0 + ragged * (_wobble(seed, ang * 1.1) + 0.5 * _wobble(seed + 3, ang * 3.3, z * 2.0)
                               + 0.35 * _wobble(seed + 5, ang * 7.3, z)) * (0.4 + 0.6 * ring)
        thick = (1.0 + lop * math.cos(ang - lop_ang)) * (1.0 + 0.08 * _wobble(seed + 9, ang * 2.0, z))
        v.co = center + ax * (x * half[0] * edge) + ay * (y * half[1] * edge) + az * (z * half[2] * thick)
    if plane is not None:
        origin, normal = Vector(plane[0]), Vector(plane[1]).normalized()
        for v in bm.verts:
            h = (v.co - origin).dot(normal)
            if h < 0.0:
                v.co -= normal * h
        bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=0.002)
        bmesh.ops.dissolve_degenerate(bm, dist=0.001, edges=list(bm.edges))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = ig._link(bpy.data.objects.new(name, me))
    # 눌린 바닥과 둘레가 만나는 모서리는 살리고 나머지는 매끈하게.
    ig.mark_sharp_by_angle(ob, 48.0)
    ig.assign_material(ob, material)
    triplanar_uv(ob)
    if bone:
        group = ob.vertex_groups.new(name=bone)
        group.add(list(range(len(me.vertices))), 1.0, "REPLACE")
    return ob


def flap(name, shape, t0, ang0, length, width, lift_deg, material, bone, seed, toward=-1.0):
    """풀려 들린 막 끝. 자루 겉면의 짧은 둘레 선(t0, ang0)에 붙어 길이 방향(toward 쪽)으로 뻗고,
    겉면에서 lift_deg만큼 들리며 끝으로 갈수록 말린다. 찢긴 끝은 열마다 길이가 다르다."""
    span = width / max(shape.radius(t0, ang0), 0.03)
    grid = []
    for i in range(4):
        s = i / 3
        row = []
        for j in range(5):
            a = ang0 + (j / 4 - 0.5) * span
            dist = length * s * (1.0 + 0.25 * _wobble(seed, j * 1.7))
            t = max(0.0, min(1.0, t0 + toward * dist / shape.length))
            lift = 0.004 + math.tan(math.radians(lift_deg)) * dist + 0.025 * s * s
            row.append(shape.point(t, a, lift))
        grid.append(row)
    ob = _sheet(name, grid, material, 0.003)
    group = ob.vertex_groups.new(name=bone)
    group.add(list(range(len(ob.data.vertices))), 1.0, "REPLACE")
    return ob


def tape_strip(name, shape, t0, t1, ang0, turns, width, material, bone, steps=22):
    """자루를 비스듬히 감은 테이프 한 줄. 한 바퀴를 다 돌지 않고 끝이 뜯겨 있다."""
    half = width * 0.5 / shape.length
    grid = []
    for i in range(steps + 1):
        s = i / steps
        t = t0 + (t1 - t0) * s
        a = ang0 + turns * math.tau * s
        grid.append([shape.point(max(0.0, t - half), a, 0.004), shape.point(min(1.0, t + half), a, 0.004)])
    ob = _sheet(name, grid, material, 0.0015)
    group = ob.vertex_groups.new(name=bone)
    group.add(list(range(len(ob.data.vertices))), 1.0, "REPLACE")
    return ob


def wrap_details(key, tag, shape, mats, bone, seed, bulges):
    """자루에 붙는 것: 찢긴 틈으로 비어져 나온 방음재, 감은 양 끝에서 비어져 나온 방음재, 풀려 들린 막 끝
    하나, 비스듬한 테이프 한 줄. 몸통 쪽 방음재는 막이 얇게 당겨진 쪽(뭉친 쪽 반대)에서 나온다. 검은 막
    사이로 노란 데가 군데군데 보여야 어둠 속에서 매끈한 검은 관으로 읽히지 않는다."""
    rnd = random.Random(seed * 97 + len(tag))
    parts = []
    for n in range(bulges):
        t = 0.16 + 0.68 * (n + rnd.uniform(0.15, 0.85)) / bulges
        ang = shape.bulge_ang + math.pi + rnd.uniform(-1.3, 1.3)
        normal = shape.direction(ang)
        tangent = shape.direction(ang + math.pi * 0.5)
        # 찢긴 틈은 비스듬하고 크기가 제각각이다(같은 크기로 늘어서면 얼룩무늬처럼 읽힌다).
        twist = rnd.uniform(-0.9, 0.9)
        along = shape.x * math.cos(twist) + tangent * math.sin(twist)
        across = normal.cross(along).normalized()
        size = (rnd.choice((0.06, 0.09, 0.13)) * rnd.uniform(0.85, 1.15), rnd.uniform(0.035, 0.065), 0.03)
        parts.append(foam_chunk(f"{key}_{tag}Foam{n}", shape.point(t, ang, -0.019), (along, across, normal), size,
                                mats["foam"], bone, seed * 13 + n + len(tag), lop=0.45, ragged=0.34, segs=12, rings=5))
    for end, t in (("A", 0.035), ("B", 0.965)):
        base = rnd.uniform(0.0, math.tau)
        for n in range(2):
            ang = base + math.pi * n + rnd.uniform(-0.6, 0.6)
            normal = shape.direction(ang)
            tangent = shape.direction(ang + math.pi * 0.5)
            parts.append(foam_chunk(f"{key}_{tag}Stuff{end}{n}", shape.point(t, ang, -0.010), (shape.x, tangent, normal),
                                    (rnd.uniform(0.035, 0.05), rnd.uniform(0.05, 0.08), 0.03), mats["foam"], bone,
                                    seed * 17 + n + len(tag) + len(end) * 5, lop=0.3, ragged=0.25, segs=8, rings=5))
    parts.append(flap(f"{key}_{tag}Flap", shape, rnd.uniform(0.62, 0.8), shape.bulge_ang + rnd.uniform(0.4, 0.9),
                      rnd.uniform(0.12, 0.17), rnd.uniform(0.07, 0.10), rnd.uniform(18.0, 28.0), mats["wrap"], bone, seed + 41))
    t0 = rnd.uniform(0.25, 0.55)
    parts.append(tape_strip(f"{key}_{tag}Tape", shape, t0, t0 + rnd.uniform(0.08, 0.14), rnd.uniform(0.0, math.tau),
                            rnd.uniform(0.62, 0.8), rnd.uniform(0.04, 0.055), mats["tape"], bone))
    return parts


def pad_clump(key, pad, d, seed, mats, bone):
    """패드: 뜯어 뭉친 계란판 방음재 덩어리 셋(짚는 면은 눌려 평평하다)에, 몸 쪽 절반을 덮은 찢긴 막과
    끊긴 실리콘 줄. 막의 몸 쪽 끝은 덩어리 밖으로 나와 짚는 면에 들러붙는다. 패드가 떨어질 때 이 끝이
    벽에 남았다가 놓인다(skin 뼈)."""
    Z = Vector((0.0, 0.0, 1.0))
    side = Vector((-d.y, d.x, 0.0))
    rnd = random.Random(seed * 53 + 11)
    flip = 1.0 if rnd.random() < 0.5 else -1.0
    plane = (pad, Z)
    chunks = [
        foam_chunk(f"{key}_PadFoam", pad + Z * 0.040, (d, side, Z), (0.25, 0.20, 0.068), mats["foam"], bone,
                   seed * 7 + 1, plane=plane, lop=0.38, ragged=0.13, segs=22, rings=10),
        foam_chunk(f"{key}_PadFoamTop", pad + d * 0.07 + side * (0.05 * flip) + Z * 0.095,
                   (_turn(d, 0.4 * flip), _turn(side, 0.4 * flip), Z), (0.15, 0.11, 0.05), mats["foam"], bone,
                   seed * 7 + 2, plane=(pad + Z * 0.05, Z), lop=0.30, ragged=0.18, segs=14, rings=7),
        foam_chunk(f"{key}_PadFoamSide", pad - d * 0.03 - side * (0.17 * flip) + Z * 0.045,
                   (_turn(d, -0.5 * flip), _turn(side, -0.5 * flip), Z), (0.11, 0.08, 0.055), mats["foam"], bone,
                   seed * 7 + 3, plane=plane, lop=0.25, ragged=0.20, segs=12, rings=6),
    ]
    bm = bmesh.new()
    for ob in chunks:
        bm.from_mesh(ob.data)
    tree = BVHTree.FromBMesh(bm)
    bm.free()
    parts = list(chunks)
    # 막 조각: 위에서 덩어리에 덮어씌운다. 덩어리를 벗어난 몸 쪽 끝은 짚는 면까지 내려와 붙는다.
    grid = []
    for i in range(10):
        u = i / 9
        row = []
        for j in range(8):
            v = j / 7 * 2.0 - 1.0
            reach = 0.36 + 0.05 * _wobble(seed, v * 2.7)
            half_w = 0.17 + 0.03 * _wobble(seed + 2, u * 3.1)
            p = pad - d * (-0.02 + u * reach) + side * (v * half_w)
            hit = tree.ray_cast(p + Z * 0.5, -Z, 1.0)
            if hit[0] is not None and hit[0].z - pad.z > 0.012:
                row.append(hit[0] + hit[1] * 0.006)
            else:
                # 덩어리 밖: 짚는 면에 붙는다. 덩어리 둘레를 뚫지 않게 조금 바깥으로 민다.
                row.append(Vector((p.x, p.y, pad.z + 0.004)) - d * 0.012)
        grid.append(row)
    drape = _sheet(f"{key}_PadMembrane", grid, mats["wrap"], 0.004)
    skin = f"skin_{key}"

    def drape_weights(co):
        height = co.z - pad.z
        inner = -(co - pad).dot(d)
        across = abs((co - pad).dot(side)) / 0.17
        w = (1.0 - _smooth((height - 0.006) / 0.045)) * _smooth((inner - 0.17) / 0.06) * (1.0 - _smooth(across / 0.55))
        return {bone: 1.0 - w, skin: w}

    _assign_weights(drape, drape_weights)
    parts.append(drape)
    SKIN_ANCHORS[key] = pad - d * 0.31 + Z * 0.008
    # 끊긴 실리콘 줄: 긴 토막 하나와 짧은 토막 둘이 덩어리 둘레 중간 높이에 붙어 있다.
    for arc, (a0, a1) in enumerate(((0.5, 1.9), (3.6, 4.0), (4.9, 5.2))):
        points = []
        for i in range(9):
            ang = a0 + (a1 - a0) * i / 8
            out = d * math.cos(ang) + side * math.sin(ang)
            origin = pad + Z * (0.032 + 0.012 * _wobble(seed + arc, ang * 2.0)) + out * 0.6
            hit = tree.ray_cast(origin, -out, 0.7)
            if hit[0] is not None:
                points.append(hit[0] + hit[1] * 0.006)
        if len(points) >= 3:
            parts.append(bead(f"{key}_PadSilicone{arc}", points, 0.010, mats["silicone"], bone))
    return parts


def web(name, body_edge, leg_edge, sag, material, bones, seed, rows=6, cols=8, thickness=0.004):
    """몸 옆구리(body_edge 두 점)에서 윗마디(leg_edge 두 점)까지 늘어진 방수막 물갈퀴.
    몸 쪽 가장자리는 몸 뼈, 다리 쪽은 윗마디 뼈를 따른다. 가운데는 물갈퀴 뼈(bones[2])를 따라
    게임이 처지게 하거나 먼저 벌어지게 민다."""
    b0, b1 = (Vector(p) for p in body_edge)
    l0, l1 = (Vector(p) for p in leg_edge)
    grid = []
    for i in range(cols + 1):
        u = i / cols
        bu = b0.lerp(b1, u)
        lu = l0.lerp(l1, u)
        row = []
        for j in range(rows + 1):
            v = j / rows
            p = bu.lerp(lu, v)
            # 가운데가 아래로 처지고, 막 주름이 길이 방향으로 잔잔하게 진다.
            p.z -= sag * 4.0 * v * (1.0 - v) * (0.6 + 0.4 * math.sin(math.pi * u))
            p.z += 0.012 * _wobble(seed, u * 5.0, v * 3.0) * math.sin(math.pi * v)
            row.append(p)
        grid.append(row)
    ob = _sheet(name, grid, material, thickness)

    # 몸 가장자리에서 다리 가장자리까지의 비율로 섞고, 가운데일수록 물갈퀴 뼈가 더 가진다.
    def weights(co):
        best = 0.0
        best_d = 1e9
        for i in range(cols + 1):
            u = i / cols
            bu = b0.lerp(b1, u)
            span = l0.lerp(l1, u) - bu
            t = max(0.0, min(1.0, (co - bu).dot(span) / max(span.length_squared, 1e-6)))
            d = (bu + span * t - co).length
            if d < best_d:
                best_d = d
                best = t
        s = _smooth(best)
        mid = 0.9 * math.sin(math.pi * best) ** 1.2
        return {bones[0]: (1.0 - s) * (1.0 - mid), bones[1]: s * (1.0 - mid), bones[2]: mid}

    _assign_weights(ob, weights)
    return ob


# --------------------------------------------------------------------------
# 생성 몸통
# --------------------------------------------------------------------------

def import_shell(path):
    """생성 GLB를 몸 치수로 맞춘 고밀도 사본. 원본 재질(색·금속성·거칠기 텍스처)을 그대로 둔다."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    meshes = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    if not meshes:
        raise RuntimeError(f"생성 몸통에 메시가 없다: {path}")
    high = ig.join(meshes, "LiningShellHigh")
    for o in [o for o in bpy.data.objects if o not in before and o.type != "MESH"]:
        bpy.data.objects.remove(o, do_unlink=True)
    bm = bmesh.new()
    bm.from_mesh(high.data)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=0.00002)
    yaw = Matrix.Rotation(math.radians(SHELL_YAW), 4, "Z")
    bmesh.ops.transform(bm, matrix=yaw, verts=bm.verts)
    bm.to_mesh(high.data)
    bm.free()
    lo, hi = ig.bounds(high)
    size = hi - lo
    scale = Vector((BODY_X / size.x, BODY_Y / size.y, BODY_Z / size.z))
    center = (lo + hi) * 0.5
    for v in high.data.vertices:
        local = v.co - Vector((center.x, center.y, lo.z))
        v.co = Vector((local.x * scale.x, local.y * scale.y, local.z * scale.z))
    high.data.update()
    for poly in high.data.polygons:
        poly.use_smooth = True
    ig.log(f"shell: {ig.triangle_count(high)} tris, scale {scale.x:.3f} {scale.y:.3f} {scale.z:.3f}")
    # 생성기가 방수막에도 금속성을 0.3쯤 넣었다. 철골(원본 0.5 이상)만 금속으로 남긴다.
    for mat in high.data.materials:
        if mat is None or not mat.use_nodes:
            continue
        tree, nodes, links = ig._nodes(mat)
        bsdf = next((n for n in nodes if n.type == "BSDF_PRINCIPLED"), None)
        if bsdf is None:
            continue
        base = bsdf.inputs["Base Color"]
        if base.is_linked:
            hsv = nodes.new("ShaderNodeHueSaturation")
            hsv.inputs["Hue"].default_value = 0.5
            hsv.inputs["Saturation"].default_value = 0.6
            hsv.inputs["Value"].default_value = 0.62
            links.new(base.links[0].from_socket, hsv.inputs["Color"])
            links.new(hsv.outputs["Color"], base)
        metal = bsdf.inputs["Metallic"]
        if metal.is_linked:
            source = metal.links[0].from_socket
            ramp = nodes.new("ShaderNodeMapRange")
            ramp.inputs["From Min"].default_value = 0.44
            ramp.inputs["From Max"].default_value = 0.58
            ramp.inputs["To Min"].default_value = 0.0
            ramp.inputs["To Max"].default_value = 0.85
            ramp.clamp = True
            links.new(source, ramp.inputs["Value"])
            links.new(ramp.outputs["Result"], metal)
        else:
            metal.default_value = 0.0
    return high


def shell_low(high, tris, placeholder):
    low = high.copy()
    low.data = high.data.copy()
    low.name = "LiningShell"
    low.data.name = "LiningShell"
    ig._link(low)
    ig.decimate(low, tris)
    ig.remove_small_islands(low, max_diameter=0.04)
    for poly in low.data.polygons:
        poly.use_smooth = True
    low.data.materials.clear()
    ig.assign_material(low, placeholder)
    layers = [l.name for l in low.data.uv_layers]
    if layers:
        low.data.uv_layers[layers[0]].name = "ShellUV"
    for layer in layers[1:]:
        low.data.uv_layers.remove(low.data.uv_layers[layer])
    triplanar_uv(low)
    attr = low.data.attributes.new("ig_shell", "INT", "FACE")
    attr.data.foreach_set("value", [1] * len(low.data.polygons))
    _assign_weights(low, mass_weights)
    return low


def surface_point(tree, origin, direction, fallback):
    """origin을 지나 direction 쪽으로 몸통 겉면 자리를 찾는다. 생성 몸통은 속에도 면이 있어서
    안에서 바깥으로 쏘면 속면에 걸린다. 바깥 먼 데서 안쪽으로 쏘아 처음 닿는 면을 쓴다."""
    out = Vector(direction).normalized()
    start = Vector(origin) + out * 2.0
    hit = tree.ray_cast(start, -out, 2.5)
    if hit[0] is None:
        return Vector(fallback), out
    normal = hit[1] if hit[1].dot(out) > 0.0 else -hit[1]
    return hit[0], normal


def bottom_outline(low, height=0.03, inset=0.012):
    """몸통 바닥 둘레(볼록 껍질)를 조금 안으로 들인 고리."""
    pts = sorted({(round(v.co.x, 4), round(v.co.y, 4)) for v in low.data.vertices if v.co.z < height})
    if len(pts) < 3:
        return []

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    hull = lower[:-1] + upper[:-1]
    cx = sum(p[0] for p in hull) / len(hull)
    cy = sum(p[1] for p in hull) / len(hull)
    ring = []
    for x, y in hull:
        d = math.hypot(x - cx, y - cy)
        k = max(d - inset, 0.0) / d if d > 1e-6 else 1.0
        ring.append(Vector((cx + (x - cx) * k, cy + (y - cy) * k, 0.012)))
    # 너무 촘촘한 점은 솎는다(관이 꼬인다).
    thinned = [ring[0]]
    for p in ring[1:]:
        if (p - thinned[-1]).length > 0.04:
            thinned.append(p)
    return thinned + thinned[:1]


# --------------------------------------------------------------------------
# 몸과 다리
# --------------------------------------------------------------------------

def leg_points(sx, sy):
    hip = Vector((HIP_X * sx, HIP_Y * sy, HIP_Z))
    d = Vector((sx, sy, 0.0)).normalized()
    knee = hip + d * SEG1
    ankle = knee + d * SEG2
    pad = ankle + d * SEG3_OUT + Vector((0.0, 0.0, -SEG3_DOWN))
    return hip, knee, ankle, pad


def build_parts(mats, shell):
    parts = [shell]
    membrane, steel, silicone, glue, iron = (mats[k] for k in ("membrane", "steel", "silicone", "glue", "iron"))
    X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
    tree = BVHTree.FromObject(shell, bpy.context.evaluated_depsgraph_get())
    # 바닥 둘레의 실리콘 테두리. 벽에 대면 이 줄이 틈을 막는다. 몸 살과 같이 처진다.
    rim = bottom_outline(shell)
    if rim:
        ob = bead("SealRim", rim, 0.014, silicone, None)
        _assign_weights(ob, mass_weights)
        parts.append(ob)
    for key, (sx, sy) in LEGS.items():
        hip, knee, ankle, pad = leg_points(sx, sy)
        d = (knee - hip).normalized()
        joint_axis = Vector((-d.y, d.x, 0.0))
        b1, b2, b3 = f"leg_{key}_1", f"leg_{key}_2", f"leg_{key}_3"
        seed = (2 if sx > 0 else 5) + (1 if sy > 0 else 0)
        # 엉덩이 받침판: 몸통 옆구리 겉면에 댄다(몸통 안의 엉덩이 볼트에서 바깥으로).
        side, side_n = surface_point(tree, Vector((hip.x, 0.0, hip.z)), Vector((0.0, sy, 0.0)), hip + Vector((0.0, 0.04 * sy, 0.0)))
        plate_center = side + side_n * 0.006
        parts.append(oriented_box(f"HipPlate_{key}", plate_center, (0.17, 0.012, 0.15), (X, Y, Z), steel, "body", bevel=0.004))
        parts.append(bolt(f"{key}_HipBolt", Vector((hip.x, side.y + 0.02 * sy, hip.z)), Vector((0.0, 1.0, 0.0)), 0.021, 0.07, iron, "body"))
        # 바깥 마디가 안쪽 마디를 품는다(접자). 무릎에서 6 cm 겹친다. 끝 마디는 패드 덩어리 속에서 끝난다.
        parts.append(c_channel(f"{key}_Seg1", hip - d * 0.05, knee + d * 0.07, (0, 0, 1), 0.135, 0.062, 0.007, steel, b1))
        parts.append(c_channel(f"{key}_Seg2", knee - d * 0.07, ankle + d * 0.06, (0, 0, -1), 0.14, 0.066, 0.008, steel, b2))
        seg3_dir = (pad - ankle).normalized()
        parts.append(c_channel(f"{key}_Seg3", ankle - seg3_dir * 0.04, ankle + (pad - ankle) * 0.70, (d.x, d.y, 0.3), 0.12, 0.056, 0.008, steel, b3))
        parts.append(bolt(f"{key}_KneeBolt", knee, joint_axis, 0.019, 0.16, iron, b2))
        parts.append(bolt(f"{key}_AnkleBolt", ankle, joint_axis, 0.017, 0.13, iron, b3))
        # 마디마다 손으로 감은 막 자루. 한쪽은 뭉쳐 두껍고 반대쪽은 얇다. 철골은 마디 끝에서만 보인다.
        upper = WrapShape(hip + d * 0.07, knee - d * 0.08, (0, 0, 1), 0.098, 0.082, 0.031, seed * 31 + 1, 0.038)
        lower = WrapShape(knee + d * 0.08, ankle - d * 0.07, (0, 0, -1), 0.088, 0.074, 0.030, seed * 31 + 2, 0.032)
        ankle_wad = WrapShape(ankle + (pad - ankle) * 0.20, ankle + (pad - ankle) * 0.78, (d.x, d.y, 0.3), 0.074, 0.064, 0.026,
                              seed * 31 + 3, 0.030, folds=5)
        for tag, shape, bone, bulges in (("Upper", upper, b1, 4), ("Lower", lower, b2, 3)):
            ob = wrap(f"{key}_{tag}Wrap", shape, mats["wrap"], bone)
            COLLISION_REF.setdefault("leg", []).extend((v.co - shape.a).cross(shape.x).length for v in ob.data.vertices)
            parts.append(ob)
            parts.extend(wrap_details(key, tag, shape, mats, bone, seed, bulges))
        parts.append(wrap(f"{key}_AnkleWrap", ankle_wad, mats["wrap"], b3, sides=16))
        # 무릎에 짜 넣은 실리콘 덩어리.
        parts.append(bead(f"{key}_KneeBead", [knee + joint_axis * 0.09 + Vector((0, 0, 0.05)), knee + Vector((0, 0, 0.075)),
                                              knee - joint_axis * 0.09 + Vector((0, 0, 0.05))], 0.016, silicone, b2))
        # 몸 옆구리에서 윗마디까지 늘어진 물갈퀴. 앞다리는 뒤쪽 옆구리로, 뒷다리는 앞쪽으로 펼쳐진다.
        # 몸통 옆구리는 둥글게 낮아진다. 옆구리 높이(18~20 cm)에서 쏴야 등 위로 넘어가지 않는다.
        b0, _ = surface_point(tree, Vector((hip.x * 0.95, 0.0, 0.18)), Vector((0.0, sy, 0.0)), hip)
        b1p, _ = surface_point(tree, Vector((hip.x - 0.42 * sx, 0.0, 0.20)), Vector((0.0, sy, 0.0)), hip - Vector((0.42 * sx, 0, 0)))
        l0 = hip + d * (SEG1 * 0.22) + Vector((0.0, 0.0, 0.10))
        l1 = hip + d * (SEG1 * 0.88) + Vector((0.0, 0.0, 0.10))
        body_edge = (b0 + Vector((0, 0, 0.01)), b1p + Vector((0, 0, 0.01)))
        parts.append(web(f"{key}_Web", body_edge, (l0, l1), 0.09, membrane, ("body", b1, f"web_{key}"), seed))
        WEB_ANCHORS[key] = (body_edge[0] + body_edge[1] + l0 + l1) * 0.25 - Vector((0.0, 0.0, 0.09))
        # 패드: 방음재 덩어리와 막 조각, 끊긴 실리콘 줄.
        pad_parts = pad_clump(key, pad, d, seed, mats, b3)
        side_dir = Vector((-d.y, d.x, 0.0))
        rel = [Vector(((v.co - pad).dot(d), (v.co - pad).dot(side_dir), v.co.z - pad.z)) for ob in pad_parts for v in ob.data.vertices]
        COLLISION_REF.setdefault("pad", []).append((Vector([min(p[i] for p in rel) for i in range(3)]),
                                                    Vector([max(p[i] for p in rel) for i in range(3)])))
        parts.extend(pad_parts)
        # 몸과 첫 마디 사이, 무릎에 늘어진 검은 접착층 실.
        p0, _ = surface_point(tree, Vector((hip.x * 0.7, 0.0, 0.30)), Vector((0.0, sy, 0.0)), hip)
        p1 = hip + (knee - hip) * 0.40 + Vector((0.0, 0.0, 0.10))
        parts.append(strand(f"{key}_Glue0", p0, p1, 0.08, 0.06, glue, ("body", b1)))
        p0 = hip + (knee - hip) * 0.80 + Vector((0.0, 0.0, 0.05))
        p1 = knee + (ankle - knee) * 0.22 + Vector((0.0, 0.0, 0.03))
        parts.append(strand(f"{key}_KneeGlue0", p0, p1, 0.07, 0.05, glue, (b1, b2)))
    return parts


def collision_reference():
    """게임 충돌 모양과 맞춰 볼 치수(cm). 다리는 자루 겉면이 뼈 줄에서 떨어진 거리의 분위수, 패드는
    패드 가운데(짚는 면) 기준 상자(바깥쪽 d, 옆, 높이)."""
    out = {}
    legs = sorted(COLLISION_REF.get("leg", []))
    if legs:
        def pick(q):
            return round(legs[min(len(legs) - 1, int(q * len(legs)))] * 100.0, 1)
        out["leg_wrap_from_bone_cm"] = {"p25": pick(0.25), "p50": pick(0.5), "p75": pick(0.75), "p95": pick(0.95),
                                        "max": round(legs[-1] * 100.0, 1)}
    pads = COLLISION_REF.get("pad", [])
    if pads:
        out["pad_box_cm"] = [{"min": [round(c * 100.0, 1) for c in lo], "max": [round(c * 100.0, 1) for c in hi]} for lo, hi in pads]
    return out


# --------------------------------------------------------------------------
# 뼈와 자세
# --------------------------------------------------------------------------

def bone_points():
    """UE 좌표의 뼈 머리·꼬리·부모. 물갈퀴·패드 막 뼈는 build_parts가 채운 자리에 둔다. 처짐·물갈퀴 뼈는
    몸 뼈와 같은 쪽(+X)을 본다. 게임이 몸 뼈에 거는 비균일 눌림이 이 뼈들에도 같은 축으로 걸린다."""
    spec = [("root", Vector((0, 0, 0)), Vector((0, 0, 0.10)), None),
            ("body", Vector((0, 0, HIP_Z)), Vector((0.30, 0, HIP_Z)), "root")]
    for key, (sx, sy) in LEGS.items():
        hip, knee, ankle, pad = leg_points(sx, sy)
        spec.append((f"leg_{key}_1", hip, knee, "body"))
        spec.append((f"leg_{key}_2", knee, ankle, f"leg_{key}_1"))
        spec.append((f"leg_{key}_3", ankle, pad, f"leg_{key}_2"))
    for key, (sx, sy) in LEGS.items():
        head = Vector((MASS_X * sx, MASS_Y * sy, MASS_Z))
        spec.append((f"mass_{key}", head, head + Vector((0.08, 0.0, 0.0)), "body"))
    for key, (sx, sy) in LEGS.items():
        hip, knee, ankle, pad = leg_points(sx, sy)
        d = (knee - hip).normalized()
        head = WEB_ANCHORS.get(key, hip + d * 0.30)
        spec.append((f"web_{key}", head, head + Vector((0.06, 0.0, 0.0)), "body"))
    for key, (sx, sy) in LEGS.items():
        hip, knee, ankle, pad = leg_points(sx, sy)
        d = (knee - hip).normalized()
        head = SKIN_ANCHORS.get(key, pad - d * 0.31)
        spec.append((f"skin_{key}", head, head - d * 0.05, f"leg_{key}_3"))
    return spec


def mirrored(v):
    return Vector((v.x, -v.y, v.z))


def build_armature(name):
    data = bpy.data.armatures.new(name + "_Armature")
    arm = ig._link(bpy.data.objects.new(name + "_Armature", data))
    ig.set_active(arm)
    bpy.ops.object.mode_set(mode="EDIT")
    for bname, head, tail, parent in bone_points():
        eb = data.edit_bones.new(bname)
        eb.head = mirrored(head)
        eb.tail = mirrored(tail)
        if parent:
            eb.parent = data.edit_bones[parent]
            eb.use_connect = False
        eb.align_roll(Vector((0.0, 0.0, 1.0)))
    bpy.ops.object.mode_set(mode="OBJECT")
    for pb in arm.pose.bones:
        pb.rotation_mode = "QUATERNION"
    return arm


def two_bone_knee(hip, ankle, l1, l2, pole):
    """엉덩이·발목·마디 길이·무릎이 갈 쪽으로 무릎 자리를 푼다(게임과 같은 계산)."""
    to = ankle - hip
    dist = max(min(to.length, l1 + l2 - 1e-4), abs(l1 - l2) + 1e-4)
    axis = to.normalized()
    a = (l1 * l1 - l2 * l2 + dist * dist) / (2.0 * dist)
    h = math.sqrt(max(l1 * l1 - a * a, 0.0))
    bend = pole - axis * pole.dot(axis)
    if bend.length < 1e-5:
        bend = Vector((0, 0, 1)) - axis * axis.z
    bend.normalize()
    return hip + axis * a + bend * h


MIRROR = Matrix.Diagonal((1.0, -1.0, 1.0))


def frame_rotation(from_dir, from_up, to_dir, to_up):
    """(from_dir, from_up) 틀을 (to_dir, to_up) 틀로 돌리는 회전(UE 좌표, 게임의 FrameRotation과 같다)."""
    def basis(direction, up):
        return Matrix(frame_from(direction, up)).transposed()
    return basis(to_dir, to_up) @ basis(from_dir, from_up).transposed()


def pose_legs(arm, body_offset, targets):
    """미리보기용 자세. targets[key] = (패드 가운데, 짚은 면의 바깥 법선, 무릎이 갈 쪽)이면 게임처럼
    패드를 면에 눕히고 발목을 거꾸로 찾아 무릎을 두 마디 IK로 풀고, ("joints", 무릎, 발목, 패드)면 그
    자리에 그대로 놓는다. 물갈퀴 뼈는 게임처럼 몸과 윗마디 사이 가운데에 둔다. 좌표는 UE."""
    pb = arm.pose.bones
    arm_mat = {}
    body_rest = arm.data.bones["body"].matrix_local.copy()
    body_mat = Matrix.Translation(mirrored(body_offset)) @ body_rest
    pb["body"].matrix = body_mat
    bpy.context.view_layer.update()
    for key, (sx, sy) in LEGS.items():
        hip0, knee0, ankle0, pad0 = leg_points(sx, sy)
        hip = hip0 + body_offset
        pad_rot = None
        if targets[key][0] == "joints":
            knee, ankle, pad = (p + body_offset for p in targets[key][1:])
        else:
            pad, normal, pole = targets[key]
            normal = normal.normalized()
            outward = Vector((sx, sy, 0.0)).normalized()
            forward = pad - hip
            forward -= normal * forward.dot(normal)
            pad_rot = frame_rotation(outward, Vector((0, 0, 1)), forward if forward.length > 1e-4 else outward, normal)
            ankle = pad - pad_rot @ (pad0 - ankle0)
            knee = two_bone_knee(hip, ankle, (knee0 - hip0).length, (ankle0 - knee0).length, pole)
        for bname, head, tail in ((f"leg_{key}_1", hip, knee), (f"leg_{key}_2", knee, ankle), (f"leg_{key}_3", ankle, pad)):
            rest = arm.data.bones[bname].matrix_local
            if bname.endswith("_3") and pad_rot is not None:
                m = (MIRROR @ pad_rot @ MIRROR @ rest.to_3x3()).to_4x4()
            else:
                y_rest = (rest.to_3x3() @ Vector((0, 1, 0))).normalized()
                z_rest = (rest.to_3x3() @ Vector((0, 0, 1))).normalized()
                y_new = (mirrored(tail) - mirrored(head)).normalized()
                rot = y_rest.rotation_difference(y_new)
                z_new = rot @ z_rest
                m = Matrix((z_new.cross(y_new).normalized(), y_new, z_new)).transposed().to_4x4()
            m.translation = mirrored(head)
            pb[bname].matrix = m
            bpy.context.view_layer.update()
        arm_mat[key] = knee
    for key in LEGS:
        web_rest = arm.data.bones[f"web_{key}"].matrix_local
        leg_rest = arm.data.bones[f"leg_{key}_1"].matrix_local
        from_body = pb["body"].matrix @ body_rest.inverted() @ web_rest
        from_leg = pb[f"leg_{key}_1"].matrix @ leg_rest.inverted() @ web_rest
        m = from_body.to_quaternion().slerp(from_leg.to_quaternion(), 0.5).to_matrix().to_4x4()
        m.translation = from_body.translation.lerp(from_leg.translation, 0.5)
        pb[f"web_{key}"].matrix = m
    bpy.context.view_layer.update()
    return arm_mat


def nudge_bone(arm, bname, offset):
    """뼈를 지금 자세에서 UE 좌표 offset만큼 옮긴다(미리보기에서 게임의 처짐·늘어짐을 흉내 낸다)."""
    pb = arm.pose.bones[bname]
    pb.matrix = Matrix.Translation(mirrored(offset)) @ pb.matrix
    bpy.context.view_layer.update()


def step_demo(arm):
    """걸음 한가운데 미리보기. 대각선 두 다리(fl·br)가 짚고 몸을 받치고, fr은 패드를 막 떼는 참이라
    패드 막 끝이 바닥에 남아 늘어난다. 받침이 없는 사분면(fr·bl)의 살과 물갈퀴가 더 처진다."""
    crouch = crouch_targets()
    pose_legs(arm, Vector((0.0, 0.0, 0.46)), crouch)
    stuck = arm.pose.bones["skin_fr"].matrix.copy()
    pad, normal, pole = crouch["fr"]
    d = Vector((1.0, 1.0, 0.0)).normalized()
    side = Vector((-d.y, d.x, 0.0))
    # 바깥 가장자리를 축으로 몸 쪽 가장자리가 먼저 들린다.
    tilt = Matrix.Rotation(math.radians(24.0), 3, side)
    pivot = pad + d * 0.24
    lifted = dict(crouch)
    lifted["fr"] = (pivot + tilt @ (pad - pivot) + Vector((0.0, 0.0, 0.02)), tilt @ normal, pole)
    pose_legs(arm, Vector((0.0, 0.0, 0.46)), lifted)
    arm.pose.bones["skin_fr"].matrix = stuck
    bpy.context.view_layer.update()
    for key, sag in (("fl", 0.025), ("br", 0.025), ("fr", 0.06), ("bl", 0.055)):
        nudge_bone(arm, f"mass_{key}", Vector((0.0, 0.0, -sag)))
    nudge_bone(arm, "web_fr", Vector((0.0, 0.0, 0.05)))
    nudge_bone(arm, "web_bl", Vector((0.0, 0.0, -0.06)))


def render_closeup(body, path, center, size, flashlight, yaw, pitch):
    """center 둘레 size(m) 상자를 화면에 채워 찍는다(몸 전체 미리보기로는 패드가 너무 작다)."""
    frame = ig.box("__closeup_frame", (size, size, size), location=(center.x, center.y, center.z))
    frame.hide_render = True
    ig.render_preview(frame, path, flashlight=flashlight, camera_yaw_deg=yaw, camera_pitch_deg=pitch, distance_scale=1.1)
    frame_mesh = frame.data
    bpy.data.objects.remove(frame, do_unlink=True)
    bpy.data.meshes.remove(frame_mesh)


def clear_pose(arm):
    for pb in arm.pose.bones:
        pb.location = (0.0, 0.0, 0.0)
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.scale = (1.0, 1.0, 1.0)
    bpy.context.view_layer.update()


def folded_joints(sx, sy):
    """접은 다리(접자를 닫은 모양). 앞다리는 옆구리 위쪽으로 뒤로 뻗었다가 앞으로 돌아오고,
    뒷다리는 아래쪽으로 앞으로 뻗었다가 돌아온다. 패드는 모서리 곁 바닥에 눕는다.
    게임의 AIGLiningEntity::FoldedJoints와 같은 수다(엉덩이 기준 오프셋, UE 좌표)."""
    hip = Vector((HIP_X * sx, HIP_Y * sy, HIP_Z))
    if sx > 0:
        knee = hip + Vector((-0.78, 0.10 * sy, 0.10))
        ankle = knee + Vector((0.76, 0.06 * sy, -0.12))
        pad = ankle + Vector((0.07, 0.11 * sy, -0.14))
    else:
        knee = hip + Vector((0.78, 0.12 * sy, -0.10))
        ankle = knee + Vector((-0.76, 0.05 * sy, 0.02))
        pad = ankle + Vector((-0.07, 0.11 * sy, -0.13))
    return knee, ankle, pad


def folded_targets():
    """쉬는 접힌 자세(바닥에 눕힌 미리보기). 벽에 붙을 때는 게임이 몸 전체를 돌린다."""
    return {key: ("joints", *folded_joints(sx, sy)) for key, (sx, sy) in LEGS.items()}


def crouch_targets():
    """바닥에서 네 다리로 버틴 자세(첫 조우·걷기). 무릎이 몸보다 높이 선다."""
    out = {}
    for key, (sx, sy) in LEGS.items():
        d = Vector((sx, sy, 0.0)).normalized()
        out[key] = (Vector((HIP_X * sx, HIP_Y * sy, 0.0)) + d * 1.15, Vector((0, 0, 1)),
                    Vector((d.x * 0.35, d.y * 0.35, 0.9)))
    return out


def spread_targets():
    """천장과 벽을 짚고 다 편 자세(5층). 앞다리는 천장, 뒷다리는 양쪽 벽."""
    return {
        "fl": (Vector((1.55, -0.95, 2.20)), Vector((0, 0, -1)), Vector((0.4, -0.6, 0.4))),
        "fr": (Vector((1.55, 0.95, 2.20)), Vector((0, 0, -1)), Vector((0.4, 0.6, 0.4))),
        "bl": (Vector((-1.10, -1.75, 0.95)), Vector((0, 1, 0)), Vector((-0.5, -0.5, 0.6))),
        "br": (Vector((-1.10, 1.75, 0.95)), Vector((0, -1, 0)), Vector((-0.5, 0.5, 0.6))),
    }


def export_rig_fbx(path, arm, mesh_ob):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.context.view_layer.update()
    for other in list(bpy.context.view_layer.objects):
        other.select_set(False)
    arm.select_set(True)
    mesh_ob.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={"ARMATURE", "MESH"},
        use_mesh_modifiers=True, mesh_smooth_type="EDGE", use_tspace=True, add_leaf_bones=False,
        primary_bone_axis="Y", secondary_bone_axis="X", armature_nodetype="NULL",
        use_armature_deform_only=True, bake_anim=False, path_mode="STRIP", embed_textures=False,
        global_scale=1.0, apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE",
        use_custom_props=False, **ig.EXPORT_AXIS)
    ig.log(f"  fbx(skeletal, no animation) -> {path} ({os.path.getsize(path)} bytes)")


# --------------------------------------------------------------------------
# 굽기: 절차 조각은 재질에서, 몸통은 고밀도 생성물에서 같은 UV로 굽고 합친다
# --------------------------------------------------------------------------

def shell_face_mask(body, size):
    """몸통 면이 차지한 UV 칸. 몸통 면은 흰 발광, 나머지는 검은 발광으로 굽는다(테두리 포함)."""
    import numpy as np
    temp = body.copy()
    temp.data = body.data.copy()
    temp.name = "LiningShellMask"
    ig._link(temp)
    temp.modifiers.clear()
    me = temp.data
    flags = [0] * len(me.polygons)
    me.attributes["ig_shell"].data.foreach_get("value", flags)
    me.materials.clear()
    for label, value in (("MaskOff", 0.0), ("MaskOn", 1.0)):
        mat = bpy.data.materials.new(label)
        tree, nodes, links = ig._nodes(mat)
        for node in list(nodes):
            if node.type != "OUTPUT_MATERIAL":
                nodes.remove(node)
        emit = nodes.new("ShaderNodeEmission")
        emit.inputs["Color"].default_value = (value, value, value, 1.0)
        links.new(emit.outputs["Emission"], nodes.get("Material Output").inputs["Surface"])
        mat["ig_kind"] = "baked"
        me.materials.append(mat)
    for poly, flag in zip(me.polygons, flags):
        poly.material_index = 1 if flag else 0
    image = ig._new_image("LiningShellMask", size, srgb=False)
    ig._bake(temp, "EMIT", image, [m for m in me.materials])
    mask = ig._pixels(image)[:, :, 0].copy()
    bpy.data.images.remove(image)
    bpy.data.objects.remove(temp, do_unlink=True)
    return mask


def composite_bakes(parts_maps, shell_maps, mask, out_paths):
    """같은 UV로 구운 두 벌을 가림판으로 합친다. 파일은 블렌더 이미지로 읽고 쓴다(색 공간 그대로)."""
    import numpy as np
    for key in ("D", "N", "ORM"):
        a = bpy.data.images.load(parts_maps[key])
        b = bpy.data.images.load(shell_maps[key])
        for img in (a, b):
            img.colorspace_settings.name = "sRGB" if key == "D" else "Non-Color"
        pa = ig._pixels(a)
        pb = ig._pixels(b)
        m = mask[:, :, None]
        out = pa * (1.0 - m) + pb * m
        out[:, :, 3] = 1.0
        result = ig._new_image(f"Lining_{key}_out", a.size[0], srgb=(key == "D"))
        result.pixels.foreach_set(out.reshape(-1))
        ig._save(result, out_paths[key])
        for img in (a, b, result):
            bpy.data.images.remove(img)
        ig.log(f"  composite {key} -> {out_paths[key]}")


def build_atlas(body, shell_density=1.25, margin=0.003):
    """구운 텍스처가 쓸 UVMap을 만든다. 스마트 UV는 둥근 자루와 생성 몸통을 면마다 조각내서
    무늬가 깍두기처럼 끊겼다. 몸통은 생성기가 편 UV 조각을, 자루·물갈퀴·패드·철골은 타일 UV
    (실제 m) 조각을 그대로 섬으로 삼는다. 몸통 섬은 겉넓이에 맞춰 m 단위로 바꾸고(조금 더 촘촘히),
    모두를 한꺼번에 채워 넣는다. 섬끼리 겹치지 않고 텍셀 밀도가 고르다."""
    me = body.data
    flags = [0] * len(me.polygons)
    me.attributes["ig_shell"].data.foreach_get("value", flags)
    if "UVMap" not in me.uv_layers:
        me.uv_layers.new(name="UVMap")
    atlas = me.uv_layers["UVMap"]
    shell_uv = me.uv_layers.get("ShellUV")
    tile = me.uv_layers["ImageUV"]

    def uv_area(layer, poly):
        pts = [layer.data[li].uv for li in poly.loop_indices]
        return abs(sum(pts[i].x * pts[(i + 1) % len(pts)].y - pts[(i + 1) % len(pts)].x * pts[i].y
                       for i in range(len(pts)))) * 0.5

    k = 1.0
    if shell_uv is not None:
        area3d = sum(p.area for p, f in zip(me.polygons, flags) if f)
        area_uv = sum(uv_area(shell_uv, p) for p, f in zip(me.polygons, flags) if f)
        if area_uv > 1e-9:
            k = math.sqrt(area3d / area_uv) * shell_density
    for poly, flag in zip(me.polygons, flags):
        for li in poly.loop_indices:
            if flag and shell_uv is not None:
                u, v = shell_uv.data[li].uv
                atlas.data[li].uv = (u * k, v * k)
            else:
                atlas.data[li].uv = tile.data[li].uv
    me.uv_layers.active = atlas
    atlas.active_render = True
    ig.set_active(body)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(rotate=True, scale=True, margin=margin)
    bpy.ops.object.mode_set(mode="OBJECT")
    ig.log(f"  atlas: shell charts x{k:.3f} (m), parts tile UV, packed together")


def bake_combined(body, high, name, out_dir, size):
    import shutil
    work = os.path.join(out_dir, "_bake_work")
    os.makedirs(work, exist_ok=True)
    parts_maps = ig.bake_textures(body, f"{name}_parts", work, size=size)
    # 몸통 면만 남긴 사본에 고밀도에서 굽는다. 다리 면이 몸통 색을 집어 오지 않는다.
    temp = body.copy()
    temp.data = body.data.copy()
    temp.name = f"{name}_shell_bake"
    ig._link(temp)
    flags = [0] * len(temp.data.polygons)
    temp.data.attributes["ig_shell"].data.foreach_get("value", flags)
    bm = bmesh.new()
    bm.from_mesh(temp.data)
    bm.faces.ensure_lookup_table()
    bmesh.ops.delete(bm, geom=[f for f, flag in zip(bm.faces, flags) if not flag], context="FACES")
    bm.to_mesh(temp.data)
    bm.free()
    temp.modifiers.clear()
    shell_maps = ig.bake_from_high(temp, high, f"{name}_shell", work, size=size,
                                   cage_extrusion=0.012, max_ray_distance=0.05)
    bpy.data.objects.remove(temp, do_unlink=True)
    mask = shell_face_mask(body, size)
    out_paths = {key: os.path.join(out_dir, f"{name}_{key}.png") for key in ("D", "N", "ORM")}
    composite_bakes(parts_maps, shell_maps, mask, out_paths)
    shutil.rmtree(work, ignore_errors=True)
    return out_paths


def main():
    args = parse_args()
    out_root = os.path.abspath(args.out) if args.out else os.path.join(SOURCE_ART, "Blender")
    name = args.name
    out_dir = os.path.join(out_root, name)
    os.makedirs(out_dir, exist_ok=True)
    ig.reset_scene()
    mats = {
        "foam": tiled_material("LiningFoam", "T_LiningFoam", 0.60, normal_strength=1.2, contact_grime=0.55),
        "membrane": tiled_material("LiningMembrane", "T_LiningMembrane", 1.0, normal_strength=0.9, rough_scale=0.62,
                                   contact_grime=0.35, tint=(0.42, 0.43, 0.46)),
        # 다리를 감은 막은 구김을 더 잘고 깊게, 반사를 조금 죽인다(긴 반사 띠가 매끈한 고무 관처럼 보였다).
        "wrap": tiled_material("LiningWrap", "T_LiningMembrane", 0.32, normal_strength=1.8, rough_scale=0.78,
                               contact_grime=0.3, tint=(0.31, 0.32, 0.34)),
        "steel": tiled_material("LiningSteel", "T_LiningSteel", 0.40, metallic=0.40, normal_strength=0.6, rough_scale=1.15,
                                tint=(0.34, 0.33, 0.31), contact_grime=0.25),
        "silicone": flat_material("LiningSilicone", (0.55, 0.55, 0.52), 0.48),
        "glue": flat_material("LiningGlue", (0.012, 0.011, 0.010), 0.22),
        "iron": flat_material("LiningBolt", (0.30, 0.29, 0.27), 0.38, metallic=0.9),
        "tape": flat_material("LiningTape", (0.035, 0.034, 0.033), 0.5),
    }
    high = import_shell(os.path.abspath(args.shell_glb))
    shell = shell_low(high, args.shell_tris, mats["membrane"])
    parts = build_parts(mats, shell)
    for ob in bpy.context.view_layer.objects:
        if ob is not None:
            ob.select_set(False)
    for ob in parts:
        ob.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    body = bpy.context.view_layer.objects.active
    body.name = name
    body.data.name = name
    lo, hi = ig.bounds(body)
    ig.log(f"{name}: tris={ig.triangle_count(body)} bounds {(hi - lo).x:.2f} x {(hi - lo).y:.2f} x {(hi - lo).z:.2f} m")
    ue_bounds = (lo.copy(), hi.copy())
    high.hide_render = True
    ig.render_preview(body, os.path.join(out_dir, f"{name}_source_preview.png"), camera_yaw_deg=-35.0, camera_pitch_deg=28.0)
    high.hide_render = False

    ig.mirror_y(body)
    ig.mirror_y(high)
    textures = {}
    if not args.no_bake:
        build_atlas(body)
        textures = bake_combined(body, high, name, out_dir, args.texture_size)
        ig.preview_material_from_bakes(body, textures)
    high_mesh = high.data
    bpy.data.objects.remove(high, do_unlink=True)
    bpy.data.meshes.remove(high_mesh)
    # 굽기가 끝나면 타일 UV와 표식은 버린다. UE는 첫 UV 채널을 구운 텍스처에 쓴다.
    for layer in [l.name for l in body.data.uv_layers if l.name != "UVMap"]:
        body.data.uv_layers.remove(body.data.uv_layers[layer])
    if "ig_shell" in body.data.attributes:
        body.data.attributes.remove(body.data.attributes["ig_shell"])

    arm = build_armature(name)
    body.parent = arm
    mod = body.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    missing = [b.name for b in arm.data.bones if b.name != "root" and b.name not in body.vertex_groups]
    if missing:
        raise RuntimeError(f"웨이트가 없는 뼈: {missing}")

    ig.render_preview(body, os.path.join(out_dir, f"{name}_preview.png"), camera_yaw_deg=-35.0, camera_pitch_deg=30.0)
    pose_legs(arm, Vector((0, 0, 0)), folded_targets())
    ig.render_preview(body, os.path.join(out_dir, f"{name}_folded.png"), camera_yaw_deg=-30.0, camera_pitch_deg=22.0)
    ig.render_preview(body, os.path.join(out_dir, f"{name}_folded_torch.png"), flashlight=True, camera_yaw_deg=-50.0, camera_pitch_deg=18.0)
    clear_pose(arm)
    pose_legs(arm, Vector((0.0, 0.0, 0.42)), crouch_targets())
    for tag, yaw, pitch in (("front", 0.0, 6.0), ("side", -90.0, 6.0), ("quarter", -40.0, 24.0)):
        ig.render_preview(body, os.path.join(out_dir, f"{name}_crouch_{tag}.png"), camera_yaw_deg=yaw, camera_pitch_deg=pitch)
    ig.render_preview(body, os.path.join(out_dir, f"{name}_crouch_torch.png"), flashlight=True, camera_yaw_deg=-25.0, camera_pitch_deg=12.0)
    clear_pose(arm)
    pose_legs(arm, Vector((0.0, 0.0, 1.35)), spread_targets())
    ig.render_preview(body, os.path.join(out_dir, f"{name}_spread.png"), camera_yaw_deg=-75.0, camera_pitch_deg=6.0)
    clear_pose(arm)
    step_demo(arm)
    ig.render_preview(body, os.path.join(out_dir, f"{name}_step.png"), camera_yaw_deg=-20.0, camera_pitch_deg=14.0)
    ig.render_preview(body, os.path.join(out_dir, f"{name}_step_torch.png"), flashlight=True, camera_yaw_deg=-20.0, camera_pitch_deg=14.0)
    # 막 끝이 바닥에 남아 늘어난 fr 패드를 옆에서 가까이 본다.
    pad_fr = crouch_targets()["fr"][0]
    render_closeup(body, os.path.join(out_dir, f"{name}_step_pad_torch.png"), mirrored(pad_fr + Vector((-0.12, -0.12, 0.16))),
                   0.95, True, 45.0, 22.0)
    clear_pose(arm)

    if args.no_bake:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, f"{name}.blend"))
        ig.log(f"{name}: previews only (--no-bake)")
        return

    slots = ig.finalize_slots(body)
    fbx_path = os.path.join(out_dir, f"{name}.fbx")
    export_rig_fbx(fbx_path, arm, body)
    ig.write_manifest(out_dir, name, "hero", fbx_path, textures, body, slots,
                      notes="덧댄 것. 생성 몸통(LiningBody trellis1024-s56)과 절차 막 자루 다리·물갈퀴·방음재 패드(rig_lining.py), "
                            "뼈 스물여섯(몸 처짐·물갈퀴·패드 막 포함), 동작 없음(게임이 뼈를 놓는다)",
                      origin="body-bottom-center", ue_bounds=ue_bounds)
    manifest_path = os.path.join(out_dir, "manifest.json")
    with open(manifest_path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    data["skeletal"] = True
    data["animations"] = {}
    data["collision_reference"] = collision_reference()
    data["generated_from"] = os.path.relpath(os.path.abspath(args.shell_glb), out_root).replace("\\", "/")
    data["bones"] = {b: {"head_cm": [round(v * 100.0, 2) for v in h], "tail_cm": [round(v * 100.0, 2) for v in t], "parent": p}
                     for b, h, t, p in bone_points()}
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, f"{name}.blend"))
    ig.log(f"{name}: done tris={ig.triangle_count(body)} bones={len(arm.data.bones)}")


if __name__ == "__main__":
    main()
