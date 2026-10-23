"""덧댄 것(SK_Lining)을 짓고 뼈를 단다.

벌어진 벽을 몸으로 덮는 괴이다. 은폐 공사에 쓰고 남은 재료로 되어 있다. 얼굴 없는 낮고 넓은 몸이다.
누렇게 산화한 계란판 방음재를 눌러 뭉친 덩어리 위에 무광 숯색 아스팔트 방수막이 큰 판으로 겹겹이
덮였고, 판은 몸 둘레를 넘어 처지며 짚는 면 가까이에서 퍼진다. 실루엣은 타원이 아니라 들쭉날쭉하게
접힌 부채꼴이다. 방음재는 막 판의 잘린 테두리와 찢긴 데에서만 보인다. 등에는 찌그러진 아연도금 ㄷ자
철골이 등뼈처럼 반쯤 묻혀 있고, 굳은 회색 실리콘이 이음매와 관절을 건너 처져 있다.

다리는 넷이다. 속은 눌리고 휜 ㄷ자 철골 띠이고, 그 위를 막 판 셋이 마디마다 다른 자리·꼬임·접힘으로
감아 다리가 꼬이며 가늘어진다. 판은 관절을 넘어 이웃 마디와 겹치고 웨이트를 섞어 같이 굽는다. 둥근
관절·볼트·깎은 경첩은 없다. 발은 짚는 면에 넓게 눌려 퍼진 비대칭 덩어리다(바닥은 평평하게 눌린
방음재, 위는 찢긴 막 판, 둘레에 번진 실리콘). 눈·입·머리·사람 손발은 없다.

몸통 속 덩어리는 생성 모델이다. 시안 `Content/SourceArt/Generated/LiningBody/source-body.png`(몸통만,
다리 붙는 자리 넷)를 TRELLIS.2로 뽑은 `trellis1024-s56/raw/pbr_00001_.glb`를 읽어 몸 치수
(80 × 120 × 40 cm)에 맞추고, 고밀도에서 색·노멀·거칠기를 굽는다. 막 판·다리·발·실리콘은 여기서 절차로
짓는다. 모습은 `Content/SourceArt/AI/LiningLegPadSheet_20261019.png`(다리·발 부품)와
`LiningStepInSitu_20261019.png`(걷는 순간), 검토 시안(큰 막 접힘, 안쪽의 눌린 철골, 넓게 퍼진 발)을
따르되 치수와 관절은 게임 동작에 맞췄다. 예전 시안은 `LiningTurnaround_20261015.png`·
`LiningModelSheet_20261013.png`·`LiningInSitu_20261013.png`다.

바인드 포즈는 몸을 바닥에 눕히고 다리 넷을 대각선으로 편 모습이다. 동작은 굽지 않는다.
게임(AIGLiningEntity)이 UPoseableMeshComponent로 뼈를 직접 놓는다. 접힌 대기, 발이 벽에서
떨어지는 순간, 두 다리로 짚고 몸을 당기는 걸음, 천장까지 편 몸, 틀에 걸린 관절이 전부 같은
뼈의 자세다. 몸이 처지고 막이 벌어지고 퍼지는 것도 게임이 뼈로 놓는다(크기를 키우지 않는다).

    blender -b --factory-startup --python Scripts/blender/rig_lining.py

뼈(UE 좌표, 미터): root(원점) → body(몸 가운데 위) → leg_{fl,fr,bl,br}_1(엉덩이→무릎) →
_2(무릎→발목) → _3(발목→발 가운데). f는 앞(+X), b는 뒤, l은 왼쪽(-Y), r은 오른쪽(+Y).
철골 띠는 뼈 하나에 통째로 붙는다(철골은 휘지 않는다). 무른 데는 뼈를 더 둔다.
- mass_{fl,…}(body 아래): 몸 사분면의 살. 등뼈 철골과 엉덩이 둘레는 body에 남고, 배와
  옆구리가 이 뼈를 따라 처지고 늘어진다.
- web_{fl,…}(body 아래): 물갈퀴 가운데. 게임이 몸과 윗마디 사이에 두고 처지거나 먼저 벌어지게 민다.
- skin_{fl,…}(leg_*_3 아래): 발 막 판의 몸 쪽 끝. 발이 떨어질 때 벽에 남아 늘어났다가 놓인다.
- skirt_{f,b,l,r}(body 아래): 몸 둘레로 늘어진 큰 막 판. 들려 있으면 처지고, 틈을 누르면 면을 따라
  바깥으로 퍼진다.
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
SHELL_TRIS = 15000
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


def _grid_object(name, grid, material):
    """점 격자(행마다 같은 수)를 면으로 잇는다. 웨이트는 부르는 쪽이 건다."""
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
    ig.assign_material(ob, material)
    return ob


def laminate(name, grid, thickness, face_material, edge_material):
    """막 판. 겉과 속은 방수막이고, 잘린 테두리(두께 면)에만 속에 붙은 방음재가 드러난다."""
    ob = _grid_object(name, grid, face_material)
    ob.data.materials.append(edge_material)
    mod = ob.modifiers.new("Solidify", "SOLIDIFY")
    mod.thickness = thickness
    mod.offset = 0.0
    mod.use_rim = True
    mod.material_offset_rim = 1
    ig.apply_modifiers(ob)
    ig.mark_sharp_by_angle(ob, 55.0)
    triplanar_uv(ob)
    return ob


def crushed_strip(name, a, b, up_hint, width, flange, thick, material, bone, seed, rings=7, crush=0.014, bow=0.008):
    """안쪽 지지대인 얇은 ㄷ자 철골 띠. 두어 군데 찌그러져 한쪽 플랜지가 안으로 접히고, 길이를 따라 살짝
    휘었다. 둥근 관이나 깎은 경첩이 아니다."""
    a = Vector(a)
    b = Vector(b)
    x, y, z = frame_from(b - a, up_hint)
    length = (b - a).length
    hw = width * 0.5
    outline = [(-hw, 0.0), (hw, 0.0), (hw, flange), (hw - thick, flange), (hw - thick, thick),
               (-hw + thick, thick), (-hw + thick, flange), (-hw, flange)]
    rnd = random.Random(seed)
    dents = [(rnd.uniform(0.2, 0.8), rnd.choice((-1.0, 1.0)), rnd.uniform(0.6, 1.0)) for _ in range(2)]
    bend = rnd.uniform(-1.0, 1.0)
    bm = bmesh.new()
    rings_v = []
    for i in range(rings + 1):
        t = i / rings
        origin = a + x * (length * t) + (z * bow + y * (bow * 0.6 * bend)) * math.sin(math.pi * t)
        ring = []
        for py, pz in outline:
            fold = sum(amount * math.exp(-(((t - td) * length) / 0.07) ** 2) for td, side, amount in dents if side * py > 0.0)
            k = pz / flange
            ring.append(bm.verts.new(origin + y * (py - math.copysign(crush * fold * k, py)) + z * (pz - crush * 0.5 * fold * k)))
        rings_v.append(ring)
    count = len(outline)
    for i in range(rings):
        for j in range(count):
            m = (j + 1) % count
            bm.faces.new((rings_v[i][j], rings_v[i][m], rings_v[i + 1][m], rings_v[i + 1][j]))
    for ring_verts, flip in ((rings_v[0], True), (rings_v[-1], False)):
        for quad in ((0, 1, 4, 5), (5, 6, 7, 0), (1, 2, 3, 4)):
            verts = [ring_verts[k] for k in quad]
            if flip:
                verts.reverse()
            try:
                bm.faces.new(verts)
            except ValueError:
                pass
    return _finish(name, bm, material, bone)


def strut_lattice(key, tag, a, b, up_hint, radius, steel, bone, seed, ext_a=0.04, ext_b=0.04, width=0.06, flange=0.024):
    """마디 속 철골: 막 바로 안쪽에 길이로 놓인 눌린 ㄷ자 띠 셋과, 이웃한 띠 사이를 비스듬히 잇는 띠 셋.
    플랜지가 바깥을 향해 막 판 틈으로 홈이 보인다. 둥근 관이나 경첩 없이 띠끼리 겹쳐 지나간다."""
    a = Vector(a)
    b = Vector(b)
    x, y, z = frame_from(b - a, up_hint)
    length = (b - a).length
    rnd = random.Random(seed)
    base = rnd.uniform(0.0, math.tau)

    def at(t, ang, rad):
        return a + x * (length * t) + (y * math.cos(ang) + z * math.sin(ang)) * rad

    def radial(ang):
        return y * math.cos(ang) + z * math.sin(ang)

    parts = []
    angles = [base + math.tau * k / 3 + rnd.uniform(-0.3, 0.3) for k in range(3)]
    twists = [rnd.uniform(-0.4, 0.4) for _ in range(3)]
    for k, ang in enumerate(angles):
        p0 = at(-ext_a / length, ang, radius)
        p1 = at(1.0 + ext_b / length, ang + twists[k], radius * 0.86)
        parts.append(crushed_strip(f"{key}_{tag}Strut{k}", p0, p1, radial(ang + twists[k] * 0.5), width, flange, 0.004,
                                   steel, bone, seed * 13 + k, crush=0.016))
    # 대각 버팀: 띠 0-1 사이에 X, 띠 1-2 사이에 하나. 띠보다 조금 안쪽이다.
    braces = ((0, 1, 0.10, 0.62), (1, 0, 0.38, 0.90), (1, 2, 0.15, 0.80))
    for n, (i, j, t0, t1) in enumerate(braces):
        ai = angles[i] + twists[i] * t0
        aj = angles[j] + twists[j] * t1
        if aj - ai > math.pi:
            aj -= math.tau
        elif ai - aj > math.pi:
            aj += math.tau
        p0 = at(t0, ai, radius * 0.9)
        p1 = at(t1, aj, radius * 0.9)
        parts.append(crushed_strip(f"{key}_{tag}Brace{n}", p0, p1, radial((ai + aj) * 0.5), width * 0.7, flange * 0.8, 0.004,
                                   steel, bone, seed * 13 + 7 + n, rings=5, crush=0.012))
    return parts


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


def leg_sheet(name, a, b, up_hint, seed, r0, layer, sweep_deg, twist_deg, ang0, ext_a, ext_b, flare, mats, weight_of,
              lift=0.0, hang=0.0, pinch_a=0.0, pinch_b=0.0, bend_dir=None, floor_z=None, rows=16, cols=9):
    """다리 마디를 감싼 막 판 하나. 단면은 길이 방향으로 한 번 접힌 V이고, 길이를 따라 꼬이며 가늘어진다.
    양 끝은 마디를 넘어 이웃 마디와 겹쳐(ext_a·ext_b m) 관절을 덮고, 웨이트를 섞어 같이 굽는다. flare가
    있으면 끝(발목)으로 갈수록 바깥으로 벌어져 발 위로 퍼진다. lift는 한쪽 가장자리가 들뜬 정도다(들뜬 판
    사이로 안쪽 철골이 보인다). hang은 한쪽 가장자리가 마디 가운데에서 아래로 처져 늘어진 깊이(m)다.
    pinch_a·pinch_b는 끝을 철골 쪽으로 오므린 정도다(관절에서 판 끝이 관 입구처럼 열려 보이지 않게).
    bend_dir이 있으면 b를 넘어간 부분은 그 방향(끝 마디)을 따라 꺾여 내려가고, floor_z 아래로는 가지 않는다
    (아랫마디 막이 발목에서 깔때기처럼 열리지 않고 발 위로 흘러내려 퍼진다). 단면에는 접힘이 한둘 이상 있고
    길이를 따라 굵기가 울퉁불퉁하다. 같은 원통이 마디마다 되풀이되지 않게 판마다 도는 자리·꼬임·접힘이 다르다."""
    a = Vector(a)
    b = Vector(b)
    x, y, z = frame_from(b - a, up_hint)
    length = (b - a).length
    rnd = random.Random(seed)
    sweep = math.radians(sweep_deg)
    twist = math.radians(twist_deg)
    fold_depth = rnd.uniform(0.02, 0.04) * rnd.choice((1.0, -0.6))
    folds = rnd.choice((1, 2, 2, 3))
    fold_phase = rnd.uniform(0.0, 0.4)
    bend = Vector(bend_dir).normalized() if bend_dir is not None else None
    t_start = -ext_a / length
    t_end = 1.0 + ext_b / length
    grid = []
    for i in range(rows + 1):
        s = i / rows
        row = []
        for j in range(cols + 1):
            u = j / cols
            # 찢긴 양 끝: 열마다 길이가 다르다.
            t0 = t_start + 0.035 * _wobble(seed, u * 4.0) / length
            t1 = t_end + 0.045 * _wobble(seed + 2, u * 4.3) / length
            t = t0 + (t1 - t0) * s
            ang = ang0 + sweep * (u - 0.5) * (1.0 + 0.12 * _wobble(seed + 4, s * 3.0)) + twist * t
            r = r0 * (1.0 - 0.24 * max(0.0, min(1.0, t))) + layer + lift * u * u
            # 접힘: 판 너비에 한둘 이상. 꼬임 때문에 길이를 따라 비스듬히 돈다.
            r += fold_depth * abs(math.sin(math.pi * (folds * u + fold_phase))) ** 1.4
            # 울퉁불퉁한 굵기: 쑤셔 넣은 방음재가 뭉친 데는 불룩하다.
            r += 0.018 * _wobble(seed + 11, t * 2.4) + 0.006 * _wobble(seed + 6, t * 6.0, u * 3.0)
            if flare:
                r += flare * _smooth((t - 0.78) / 0.3) ** 1.5
            if pinch_a:
                r *= 1.0 - pinch_a * (1.0 - _smooth((t - t0) / 0.16))
            if pinch_b:
                r *= 1.0 - pinch_b * _smooth((t - (t1 - 0.16)) / 0.16)
            radial = y * math.cos(ang) + z * math.sin(ang)
            if bend is not None and t > 1.0:
                p = b + bend * (length * (t - 1.0)) + radial * r
            else:
                p = a + x * (length * t) + radial * r
            if floor_z is not None:
                p.z = max(p.z, floor_z + 0.006)
            if hang:
                # 처지는 가장자리: 마디 가운데일수록 깊고, 관절 쪽은 붙어 있다. 짚는 면 아래로는 내려가지 않는다.
                edge = _smooth((u - 0.5) / 0.5) ** 1.4
                env = 0.25 + 0.75 * math.sin(math.pi * max(0.0, min(1.0, t)))
                droop = hang * edge * env * (1.0 + 0.3 * _wobble(seed + 8, t * 5.0))
                p = p + Vector((0.0, 0.0, -droop)) + radial * (0.45 * droop)
                p.z = max(p.z, 0.01)
            row.append(p)
        grid.append(row)
    ob = laminate(name, grid, 0.014, mats["membrane"], mats["foam"])
    _assign_weights(ob, weight_of)
    return ob


def segment_weights(start, axis, length, own, before=None, after=None, before_reach=0.12, after_reach=0.14):
    """마디 막 판의 웨이트. 마디 안은 제 뼈, 앞뒤로 넘어간 자리는 이웃 뼈와 섞는다(관절에서 같이 굽는다)."""
    start = Vector(start)
    axis = Vector(axis).normalized()

    def weights(co):
        s = (co - start).dot(axis)
        out = {own: 1.0}
        if before:
            w = 1.0 - _smooth((s + 0.02) / before_reach)
            out[before] = w
            out[own] -= w
        if after:
            w = _smooth((s - (length - 0.06)) / after_reach)
            out[after] = w
            out[own] -= w
        out[own] = max(out[own], 0.0)
        return out

    return weights


def foot(key, pad, d, seed, mats, bone):
    """발: 짚는 면에 넓게 눌려 퍼진 비대칭 덩어리. 바닥은 평평하게 눌린 방음재이고 위는 찢긴 방수막 판이
    덮었다. 막이 덮지 못한 둘레와 판의 잘린 테두리에서만 방음재가 보인다. 굳은 실리콘이 짚는 면 둘레에
    눌려 번졌다. 막의 몸 쪽 끝은 짚는 면까지 내려와 들러붙는다. 발이 떨어질 때 이 끝이 면에 남았다가
    놓인다(skin 뼈)."""
    Z = Vector((0.0, 0.0, 1.0))
    side = Vector((-d.y, d.x, 0.0))
    rnd = random.Random(seed * 53 + 11)
    flip = 1.0 if rnd.random() < 0.5 else -1.0
    plane = (pad, Z)
    chunks = [
        foam_chunk(f"{key}_FootFoam", pad + Z * 0.028 + d * 0.02, (d, side, Z), (0.31, 0.25, 0.045), mats["foam"], bone,
                   seed * 7 + 1, plane=plane, lop=0.5, ragged=0.16, segs=22, rings=8),
        foam_chunk(f"{key}_FootLobe", pad + d * 0.07 - side * (0.21 * flip) + Z * 0.022,
                   (_turn(d, 0.5 * flip), _turn(side, 0.5 * flip), Z), (0.16, 0.12, 0.035), mats["foam"], bone,
                   seed * 7 + 3, plane=plane, lop=0.3, ragged=0.22, segs=14, rings=6),
    ]
    bm = bmesh.new()
    for ob in chunks:
        bm.from_mesh(ob.data)
    tree = BVHTree.FromBMesh(bm)
    bm.free()
    parts = list(chunks)
    # 막 판: 위에서 덩어리에 덮어씌운다. 둘레 몇 군데는 짚는 면까지 흘러내리고, 몸 쪽 끝은 면에 붙는다.
    grid = []
    for i in range(12):
        u = i / 11
        row = []
        for j in range(10):
            v = j / 9 * 2.0 - 1.0
            reach_out = 0.24 + 0.05 * _wobble(seed, v * 2.3)
            reach_in = 0.36 + 0.04 * _wobble(seed + 1, v * 2.9)
            along = reach_out - (reach_out + reach_in) * u
            half_w = 0.22 + 0.04 * _wobble(seed + 2, u * 3.1)
            p = pad + d * along + side * (v * half_w)
            hit = tree.ray_cast(p + Z * 0.5, -Z, 1.0)
            if hit[0] is not None and hit[0].z - pad.z > 0.010:
                row.append(hit[0] + hit[1] * 0.006)
            else:
                # 덩어리 밖: 짚는 면까지 흘러내려 붙는다. 덩어리 둘레를 뚫지 않게 조금 바깥으로 민다.
                out = (p - pad)
                out.z = 0.0
                row.append(Vector((p.x, p.y, pad.z + 0.004)) + (out.normalized() * 0.012 if out.length > 1e-4 else Vector()))
        grid.append(row)
    drape = laminate(f"{key}_FootMembrane", grid, 0.007, mats["membrane"], mats["foam"])
    skin = f"skin_{key}"

    def drape_weights(co):
        height = co.z - pad.z
        inner = -(co - pad).dot(d)
        across = abs((co - pad).dot(side)) / 0.22
        w = (1.0 - _smooth((height - 0.006) / 0.06)) * _smooth((inner - 0.12) / 0.2) ** 1.3 * (1.0 - _smooth(across / 0.6))
        return {bone: 1.0 - w, skin: w}

    _assign_weights(drape, drape_weights)
    parts.append(drape)
    SKIN_ANCHORS[key] = pad - d * 0.33 + Z * 0.008
    # 짚는 면 둘레에 눌려 번진 실리콘: 끊긴 굵은 줄 둘.
    for arc, (a0, a1) in enumerate(((0.4, 1.7), (3.5, 4.4))):
        points = []
        for i in range(9):
            ang = a0 + (a1 - a0) * i / 8
            out = d * math.cos(ang) + side * math.sin(ang)
            origin = pad + Z * (0.012 + 0.006 * _wobble(seed + arc, ang * 2.0)) + out * 0.7
            hit = tree.ray_cast(origin, -out, 0.8)
            if hit[0] is not None:
                q = hit[0] + hit[1] * 0.008
                q.z = max(q.z, pad.z + 0.015)
                points.append(q)
        if len(points) >= 3:
            parts.append(bead(f"{key}_FootSealant{arc}", points, 0.014, mats["sealant"], bone))
    return parts


def web(name, body_edge, leg_edge, sag, mats, bones, seed, rows=6, cols=8):
    """몸 옆구리(body_edge 두 점)에서 윗마디(leg_edge 두 점)까지 늘어진 방수막 물갈퀴(두께 있는 판).
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
    ob = laminate(name, grid, 0.006, mats["membrane"], mats["foam"])

    # 몸 가장자리에서 다리 가장자리까지의 비율로 섞고, 가운데일수록 물갈퀴 뼈가 더 가진다.
    def weights(co):
        best = 0.0
        best_d = 1e9
        for i in range(cols + 1):
            u = i / cols
            bu = b0.lerp(b1, u)
            span = l0.lerp(l1, u) - bu
            t = max(0.0, min(1.0, (co - bu).dot(span) / max(span.length_squared, 1e-6)))
            dist = (bu + span * t - co).length
            if dist < best_d:
                best_d = dist
                best = t
        s = _smooth(best)
        mid = 0.9 * math.sin(math.pi * best) ** 1.2
        return {bones[0]: (1.0 - s) * (1.0 - mid), bones[1]: s * (1.0 - mid), bones[2]: mid}

    _assign_weights(ob, weights)
    return ob


SKIRT_SIDES = {"f": Vector((1.0, 0.0, 0.0)), "b": Vector((-1.0, 0.0, 0.0)), "l": Vector((0.0, -1.0, 0.0)),
               "r": Vector((0.0, 1.0, 0.0))}


def skirt_weights(co):
    """몸 둘레 쪽 무게. 몸 가운데에서 본 방향으로 앞·뒤·왼쪽·오른쪽 치마 뼈에 나눈다."""
    flat = Vector((co.x / 0.40, co.y / 0.60, 0.0))
    if flat.length < 1e-4:
        return {"skirt_f": 0.25, "skirt_b": 0.25, "skirt_l": 0.25, "skirt_r": 0.25}
    flat.normalize()
    out = {f"skirt_{k}": max(0.0, flat.dot(v)) ** 2 for k, v in SKIRT_SIDES.items()}
    total = sum(out.values())
    return {k: w / total for k, w in out.items()}


def body_drape(name, shell_tree, front, seed, mats, layer=0.0, x0=0.07, overhang=0.15, pleats=0.22):
    """몸통에 덮어씌운 큰 방수막 판(앞 또는 뒤 절반). 등뼈 철골 옆에서 시작해 몸통 위를 덮고, 몸 둘레를 넘어
    아래로 처지며 짚는 면 높이 가까이에서 바깥으로 퍼진다. 접힘(주름)은 가장자리로 갈수록 깊다. 몸 위는
    살 처짐 뼈를, 몸 밖으로 늘어진 데는 치마 뼈를, 엉덩이 근처는 윗마디를 따른다. 짚는 면(z 0) 아래로는
    내려가지 않는다."""
    sign = 1.0 if front else -1.0
    rnd = random.Random(seed)
    phase = rnd.uniform(0.0, math.tau)
    rows, cols = 22, 12
    grid = []
    for i in range(rows + 1):
        yy = -0.66 + 1.32 * i / rows
        # 몸통 둘레(x)와 그 높이를 옆에서 쏘아 찾는다.
        hit = shell_tree.ray_cast(Vector((sign * 1.2, yy, 0.16)), Vector((-sign, 0.0, 0.0)), 1.3)
        edge_x = abs(hit[0].x) if hit[0] is not None else 0.40 * math.sqrt(max(0.0, 1.0 - (yy / 0.62) ** 2))
        reach = overhang * (0.8 + 0.5 * _wobble(seed, yy * 3.1)) + 0.02 * abs(yy) / 0.6
        row = []
        for j in range(cols + 1):
            u = j / cols
            span = max(edge_x + reach - x0, 0.05)
            xx = x0 + span * u
            beyond = max(0.0, xx - edge_x)
            top = shell_tree.ray_cast(Vector((sign * min(xx, edge_x - 0.005), yy, 1.0)), Vector((0.0, 0.0, -1.0)), 1.2)
            z_top = (top[0].z if top[0] is not None else 0.12) + 0.018 + layer
            if beyond <= 0.0:
                z = z_top
            else:
                k = _smooth(beyond / max(reach, 0.01))
                z = 0.03 + layer * 0.5 + (z_top - 0.03) * (1.0 - k) ** 1.6
            # 접힘: 가장자리로 갈수록 깊어지는 주름이 몸 길이를 따라 물결친다.
            depth = (0.006 + 0.052 * u ** 1.5) * (1.0 + 0.3 * _wobble(seed + 3, yy * 2.0))
            z += depth * math.sin(math.tau * yy / pleats + phase + 0.6 * _wobble(seed + 5, u * 2.0))
            xx += 0.025 * u ** 2 * _wobble(seed + 7, yy * 5.0)
            # 엉덩이 근처에서는 다리 위로 걸쳐 놓인다(다리를 뚫지 않게 들어 올린다).
            p = Vector((sign * xx, yy, max(z, 0.012)))
            for hx, hy in ((HIP_X * sign, HIP_Y), (HIP_X * sign, -HIP_Y)):
                hip = Vector((hx, hy, HIP_Z))
                dd = Vector((hx, hy, 0.0)).normalized()
                rel = p - hip
                along = rel.dot(dd)
                lateral = (rel - dd * along)
                lateral.z = 0.0
                if -0.05 < along < 0.45 and lateral.length < 0.17:
                    lift = (1.0 - _smooth(lateral.length / 0.17)) * (1.0 - _smooth((along - 0.30) / 0.15))
                    p.z = max(p.z, HIP_Z + 0.13 * lift + (p.z - HIP_Z) * (1.0 - lift))
            row.append(p)
        grid.append(row)
    ob = laminate(name, grid, 0.02, mats["membrane"], mats["foam"])

    def weights(co):
        edge = abs(co.x) - 0.38
        on_body = Vector((max(-0.40, min(0.40, co.x)), max(-0.60, min(0.60, co.y)), max(0.0, min(0.40, co.z))))
        base = mass_weights(on_body)
        k = _smooth(edge / 0.16) * 0.9
        out = {bone: w * (1.0 - k) for bone, w in base.items()}
        for bone, w in skirt_weights(co).items():
            out[bone] = out.get(bone, 0.0) + w * k
        for leg_key, (sx, sy) in LEGS.items():
            hip = Vector((HIP_X * sx, HIP_Y * sy, HIP_Z))
            dd = Vector((sx, sy, 0.0)).normalized()
            rel = co - hip
            along = rel.dot(dd)
            lateral = rel - dd * along
            lateral.z = 0.0
            if along > -0.05 and lateral.length < 0.17:
                wl = (1.0 - _smooth(lateral.length / 0.17)) * _smooth((along + 0.05) / 0.25) * 0.85
                out = {bone: w * (1.0 - wl) for bone, w in out.items()}
                out[f"leg_{leg_key}_1"] = out.get(f"leg_{leg_key}_1", 0.0) + wl
        return out

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
        rough = bsdf.inputs["Roughness"]
        if rough.is_linked:
            # 생성기는 막을 젖은 플라스틱처럼 번들거리게 구웠다. 무광 쪽으로 올린다.
            lift = nodes.new("ShaderNodeMapRange")
            lift.inputs["To Min"].default_value = 0.5
            lift.inputs["To Max"].default_value = 1.0
            links.new(rough.links[0].from_socket, lift.inputs["Value"])
            links.new(lift.outputs["Result"], rough)
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
    membrane, steel, sealant = mats["membrane"], mats["steel"], mats["sealant"]
    Z = Vector((0, 0, 1))
    tree = BVHTree.FromObject(shell, bpy.context.evaluated_depsgraph_get())
    # 바닥 둘레의 실리콘 테두리. 벽에 대면 이 줄이 틈을 막는다. 몸 살과 같이 처진다.
    rim = bottom_outline(shell)
    if rim:
        ob = bead("SealRim", rim, 0.014, sealant, None)
        _assign_weights(ob, mass_weights)
        parts.append(ob)
    # 몸통을 덮은 큰 막 판 둘(앞·뒤 절반)과 한쪽 모서리에 겹친 작은 판 하나. 몸 실루엣이 타원 대신
    # 들쭉날쭉하게 접힌 부채꼴이 된다.
    parts.append(body_drape("DrapeFront", tree, True, 71, mats, layer=0.0, overhang=0.22, pleats=0.26))
    parts.append(body_drape("DrapeBack", tree, False, 83, mats, layer=0.008, overhang=0.20, pleats=0.21))
    parts.append(body_drape("DrapeCorner", tree, True, 97, mats, layer=0.028, x0=0.20, overhang=0.14, pleats=0.17))
    for key, (sx, sy) in LEGS.items():
        hip, knee, ankle, pad = leg_points(sx, sy)
        d = (knee - hip).normalized()
        side_d = Vector((-d.y, d.x, 0.0))
        b1, b2, b3 = f"leg_{key}_1", f"leg_{key}_2", f"leg_{key}_3"
        seed = (2 if sx > 0 else 5) + (1 if sy > 0 else 0)
        rnd = random.Random(seed * 101)
        # 안쪽 지지대: 막 바로 안쪽에 눌린 ㄷ자 띠를 길이로 셋, 그 사이를 비스듬히 잇는 띠를 셋 둔다. 엉덩이 쪽 띠는
        # 몸통 속으로 들어가 몸에 물리고, 무릎에서는 위아래 마디의 띠가 겹쳐 지나갈 뿐 둥근 경첩이나 볼트가 없다.
        parts.extend(strut_lattice(key, "Upper", hip, knee, (0, 0, 1), 0.085, steel, b1, seed * 11 + 1, ext_a=0.12, ext_b=0.06))
        parts.extend(strut_lattice(key, "Lower", knee, ankle, (0, 0, -1), 0.072, steel, b2, seed * 11 + 5, ext_a=0.06, ext_b=0.03,
                                   width=0.052, flange=0.02))
        # 막 판: 마디마다 감싼 판 셋과 위에 걸쳐 한쪽이 처진 판 하나. 감싼 판 사이에 틈을 두어 철골 띠가 보인다.
        # 도는 자리·꼬임·접힘이 판마다 다르고, 관절을 넘어 이웃 마디와 겹친다. 무릎 쪽 끝은 철골로 오므린다.
        base = rnd.uniform(0.0, math.tau)
        w1 = segment_weights(hip, d, SEG1, b1, before="body", after=b2)
        w2 = segment_weights(knee, d, SEG2, b2, before=b1, after=b3, after_reach=0.10)
        seg3_dir = (pad - ankle).normalized()
        for k in range(3):
            ang = base + math.tau * k / 3 + rnd.uniform(-0.3, 0.3)
            parts.append(leg_sheet(f"{key}_Sheet1_{k}", hip, knee, (0, 0, 1), seed * 17 + k, 0.108, 0.018 * k,
                                   rnd.uniform(80, 105), rnd.choice((-1, 1)) * rnd.uniform(40, 90), ang,
                                   0.05, 0.09, 0.0, mats, w1, lift=rnd.choice((0.0, 0.03)), pinch_b=0.28))
            ang = base + 0.7 + math.tau * k / 3 + rnd.uniform(-0.3, 0.3)
            parts.append(leg_sheet(f"{key}_Sheet2_{k}", knee, ankle, (0, 0, -1), seed * 19 + k, 0.090, 0.016 * k,
                                   rnd.uniform(75, 100), rnd.choice((-1, 1)) * rnd.uniform(40, 90), ang,
                                   0.08, 0.17, 0.12, mats, w2, lift=rnd.choice((0.0, 0.02)), pinch_a=0.24,
                                   bend_dir=seg3_dir, floor_z=pad.z))
        # 위에 걸친 판: 마디 위쪽을 덮고 한쪽 가장자리가 마디 가운데에서 아래로 처진다.
        top = math.pi * 0.5 + rnd.uniform(-0.5, 0.5)
        parts.append(leg_sheet(f"{key}_Flap1", hip, knee, (0, 0, 1), seed * 23, 0.112, 0.05,
                               rnd.uniform(120, 150), rnd.choice((-1, 1)) * rnd.uniform(20, 50), top,
                               0.03, 0.06, 0.0, mats, w1, hang=rnd.uniform(0.12, 0.17), pinch_b=0.2))
        top = -math.pi * 0.5 + rnd.uniform(-0.5, 0.5)
        parts.append(leg_sheet(f"{key}_Flap2", knee, ankle, (0, 0, -1), seed * 29, 0.094, 0.046,
                               rnd.uniform(110, 140), rnd.choice((-1, 1)) * rnd.uniform(20, 50), top,
                               0.05, 0.02, 0.06, mats, w2, hang=rnd.uniform(0.08, 0.12), pinch_a=0.18))
        COLLISION_REF.setdefault("leg_shapes", []).append((hip, knee, ankle))
        # 굳은 실리콘이 관절을 건너 처진다(무릎: 윗마디→아랫마디, 엉덩이: 몸→윗마디).
        p0 = knee - d * 0.14 + Z * 0.09 + side_d * 0.03
        p1 = knee + d * 0.16 + Z * 0.07 - side_d * 0.02
        parts.append(strand(f"{key}_KneeSealant", p0, p1, 0.06, 0.035, sealant, (b1, b2)))
        p0, _ = surface_point(tree, Vector((hip.x * 0.7, 0.0, 0.30)), Vector((0.0, sy, 0.0)), hip)
        p1 = hip + d * 0.28 + Z * 0.10
        parts.append(strand(f"{key}_HipSealant", p0, p1, 0.07, 0.04, sealant, ("body", b1)))
        # 몸 옆구리에서 윗마디까지 늘어진 물갈퀴. 앞다리는 뒤쪽 옆구리로, 뒷다리는 앞쪽으로 펼쳐진다.
        # 몸통 옆구리는 둥글게 낮아진다. 옆구리 높이(18~20 cm)에서 쏴야 등 위로 넘어가지 않는다.
        b0, _ = surface_point(tree, Vector((hip.x * 0.95, 0.0, 0.18)), Vector((0.0, sy, 0.0)), hip)
        b1p, _ = surface_point(tree, Vector((hip.x - 0.42 * sx, 0.0, 0.20)), Vector((0.0, sy, 0.0)), hip - Vector((0.42 * sx, 0, 0)))
        l0 = hip + d * (SEG1 * 0.22) + Vector((0.0, 0.0, 0.08))
        l1 = hip + d * (SEG1 * 0.92) + Vector((0.0, 0.0, 0.08))
        body_edge = (b0 + Vector((0, 0, 0.01)), b1p + Vector((0, 0, 0.01)))
        parts.append(web(f"{key}_Web", body_edge, (l0, l1), 0.10, mats, ("body", b1, f"web_{key}"), seed))
        WEB_ANCHORS[key] = (body_edge[0] + body_edge[1] + l0 + l1) * 0.25 - Vector((0.0, 0.0, 0.09))
        # 발: 짚는 면에 넓게 눌려 퍼진 덩어리와 막, 번진 실리콘.
        foot_parts = foot(key, pad, d, seed, mats, b3)
        rel = [Vector(((v.co - pad).dot(d), (v.co - pad).dot(side_d), v.co.z - pad.z)) for ob in foot_parts for v in ob.data.vertices]
        COLLISION_REF.setdefault("pad", []).append((Vector([min(p[i] for p in rel) for i in range(3)]),
                                                    Vector([max(p[i] for p in rel) for i in range(3)])))
        parts.extend(foot_parts)
    return parts


def collision_reference(body):
    """게임 충돌 모양과 맞춰 볼 치수(cm). 다리는 윗마디·아랫마디에 붙은 정점이 뼈 줄에서 떨어진 거리의
    분위수(막 판·지지대 모두), 발은 짚는 면 기준 상자(바깥쪽 d, 옆, 높이)."""
    out = {}
    me = body.data
    distances = []
    for key, (hip, knee, ankle) in zip(LEGS, COLLISION_REF.get("leg_shapes", [])):
        for segment, (a, b) in enumerate(((hip, knee), (knee, ankle))):
            group = body.vertex_groups.get(f"leg_{key}_{segment + 1}")
            if group is None:
                continue
            axis = (b - a).normalized()
            length = (b - a).length
            for v in me.vertices:
                w = next((g.weight for g in v.groups if g.group == group.index), 0.0)
                if w < 0.5:
                    continue
                s = (v.co - a).dot(axis)
                if 0.08 < s < length - 0.08:
                    distances.append((v.co - a).cross(axis).length)
    distances.sort()
    if distances:
        def pick(q):
            return round(distances[min(len(distances) - 1, int(q * len(distances)))] * 100.0, 1)
        out["leg_from_bone_cm"] = {"p25": pick(0.25), "p50": pick(0.5), "p75": pick(0.75), "p95": pick(0.95),
                                   "max": round(distances[-1] * 100.0, 1)}
    pads = COLLISION_REF.get("pad", [])
    if pads:
        out["foot_box_cm"] = [{"min": [round(c * 100.0, 1) for c in lo], "max": [round(c * 100.0, 1) for c in hi]} for lo, hi in pads]
    return out


# --------------------------------------------------------------------------
# 뼈와 자세
# --------------------------------------------------------------------------

def bone_points():
    """UE 좌표의 뼈 머리·꼬리·부모. 물갈퀴·패드 막 뼈는 build_parts가 채운 자리에 둔다. 처짐·물갈퀴·치마
    뼈는 몸 뼈와 같은 쪽(+X)을 본다. 게임이 몸 뼈에 거는 비균일 눌림이 이 뼈들에도 같은 축으로 걸린다."""
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
        head = SKIN_ANCHORS.get(key, pad - d * 0.33)
        spec.append((f"skin_{key}", head, head - d * 0.05, f"leg_{key}_3"))
    for key, side in SKIRT_SIDES.items():
        head = Vector((side.x * 0.44, side.y * 0.66, 0.08))
        spec.append((f"skirt_{key}", head, head + Vector((0.06, 0.0, 0.0)), "body"))
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
    for key, sag in (("f", 0.05), ("b", 0.06), ("l", 0.07), ("r", 0.08)):
        nudge_bone(arm, f"skirt_{key}", Vector((0.0, 0.0, -sag)))
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
    # AO 광선은 8 cm 안의 가림만 센다. 기본 거리로는 몸에 1~2 cm 떠 덮인 막 판이 전부 가려진 것으로 구워졌다.
    world = bpy.context.scene.world or bpy.data.worlds.new("LiningBakeWorld")
    bpy.context.scene.world = world
    world.light_settings.distance = 0.08
    # 생성 고밀도 몸통은 저밀도 몸통과 같은 자리에 겹쳐 있다. 부품을 구울 때 렌더에 남으면 AO 광선이 바로
    # 거기 막혀 막 판과 몸통의 AO가 0으로 구워졌다(2026-10-22, 쓰는 텍셀의 56%). 그러면 엔진이 간접광을
    # 다 막아 손전등 밖의 막이 검은 구멍처럼 보였다.
    high.hide_render = True
    parts_maps = ig.bake_textures(body, f"{name}_parts", work, size=size)
    high.hide_render = False
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
    # 몸통 사본의 AO도 같은 자리의 원래 몸에 막히지 않게 원래 몸을 렌더에서 뺀다.
    body.hide_render = True
    shell_maps = ig.bake_from_high(temp, high, f"{name}_shell", work, size=size,
                                   cage_extrusion=0.012, max_ray_distance=0.05)
    body.hide_render = False
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
    # 막은 숯색 아스팔트 방수막(번들거리는 검은 플라스틱이 아니다), 방음재는 누런 회색, 철골은 둔한
    # 아연도금, 실리콘은 굳은 회색이다. 막은 타일 색(반사율 5%쯤, 실제 아스팔트 막과 비슷하다)을 조금만
    # 눌러 4%대로 둔다. 예전에는 0.15를 곱해 1%도 안 됐고, 밤 손전등 아래에서 접힘도 재료도 안 보이는 검은
    # 다각형이 됐다(2026-10-22 밤4 벽 진단). 거칠기는 큰 접힘에 손전등이 비칠 만큼만 낮춘다.
    mats = {
        "foam": tiled_material("LiningFoam", "T_LiningFoam", 0.45, normal_strength=1.2, contact_grime=0.4),
        "membrane": tiled_material("LiningMembrane", "T_LiningMembrane", 0.7, normal_strength=1.2, rough_scale=1.05,
                                   contact_grime=0.3, tint=(0.8, 0.8, 0.78)),
        "steel": tiled_material("LiningSteel", "T_LiningSteel", 0.35, metallic=0.3, normal_strength=0.7, rough_scale=1.6,
                                tint=(0.30, 0.30, 0.29), contact_grime=0.3),
        "sealant": flat_material("LiningSealant", (0.10, 0.10, 0.088), 0.6),
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
    collision = collision_reference(body)
    ig.log(f"{name}: collision {json.dumps(collision)[:400]}")
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
                      notes="덧댄 것. 생성 몸통(LiningBody trellis1024-s56)에 절차로 지은 큰 막 판·눌린 ㄷ자 띠 다리·방음재 발"
                            "(rig_lining.py), 뼈 서른(몸 처짐·물갈퀴·발 막·치마 포함), 동작 없음(게임이 뼈를 놓는다)",
                      origin="body-bottom-center", ue_bounds=ue_bounds)
    manifest_path = os.path.join(out_dir, "manifest.json")
    with open(manifest_path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    data["skeletal"] = True
    data["animations"] = {}
    data["collision_reference"] = collision
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
