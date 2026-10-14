"""덧댄 것(SK_Lining)을 절차로 짓고 뼈를 단다.

벌어진 벽을 몸으로 덮는 괴이다. 은폐 공사에 쓰고 남은 재료로 되어 있다. 몸의 중심은 아래의
누르는 판이다. 아스팔트 방수막으로 싼 어두운 판이고, 테두리에 굳은 실리콘 줄이 둘려 있어
벽에 대면 틈을 막는다. 그 위에 누렇게 산화한 계란판 방음재 두 겹이 등처럼 얹혀 있고, 아연도금
ㄷ자 철골 등뼈 하나가 앞뒤 엉덩이 가로대를 잇는다. 다리는 넷이고 마디마다 볼트로 접히는
접자처럼 ㄷ자 철골 세 마디다. 끝은 방수막으로 싼 넓적한 방음재 패드라서 벽과 천장을 눌러
짚는다. 눈·입·머리·사람 손발은 없다. 몸과 다리 사이에는 검은 접착층이 실처럼 늘어진다.
아래를 향한 면(누르는 면, 패드 바닥)에만 눌리고 끌린 때가 있다.

시안: Content/SourceArt/AI/LiningModelSheet_20261013.png(접힘·반쯤 폄·다 폄),
LiningInSitu_20261013.png(관리실 안쪽). 재질은 같은 날 뽑은 타일 셋을 손본 것이다
(T_LiningFoam·T_LiningMembrane·T_LiningSteel의 D·N·R).

바인드 포즈는 몸판을 바닥에 눕히고 다리 넷을 대각선으로 편 모습이다. 동작은 굽지 않는다.
게임(AIGLiningEntity)이 UPoseableMeshComponent로 뼈를 직접 놓는다. 접힌 대기, 패드가 벽에서
떨어지는 순간, 두 다리로 짚고 몸을 당기는 걸음, 천장까지 편 몸, 틀에 걸린 관절이 전부 같은
뼈 열세 개의 자세다.

    blender -b --factory-startup --python Scripts/blender/rig_lining.py

뼈(UE 좌표, 미터): root(원점) → body(몸판 가운데 위) → leg_{fl,fr,bl,br}_1(엉덩이→무릎) →
_2(무릎→발목) → _3(발목→패드 가운데). f는 앞(+X), b는 뒤, l은 왼쪽(-Y), r은 오른쪽(+Y).
조각마다 뼈 하나에 통째로 붙는다(철골은 휘지 않는다). 접착층 실만 양 끝 뼈를 섞는다.
"""

import argparse
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import bpy  # noqa: E402
import bmesh  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import ig_blender_lib as ig  # noqa: E402

SOURCE_ART = os.path.abspath(os.path.join(HERE, "..", "..", "Content", "SourceArt"))

# 몸판(접혔을 때 벽에 기대면 높이 0.8 m, 폭 1.2 m가 된다).
BODY_X = 0.80
BODY_Y = 1.20
BODY_Z = 0.40
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


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", default="SK_Lining")
    parser.add_argument("--texture-size", type=int, default=2048)
    parser.add_argument("--out", default=None)
    parser.add_argument("--no-bake", action="store_true")
    return parser.parse_args(argv)


# --------------------------------------------------------------------------
# 재질
# --------------------------------------------------------------------------

def tiled_material(name, stem, tile_m, metallic=0.0, normal_strength=1.0, rough_scale=1.0, contact_grime=0.0):
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

    base = image("D", "sRGB")
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

        def socket(sockets, name):
            # 섞기 노드는 자료형마다 같은 이름의 소켓을 따로 둔다. 색 소켓을 고른다.
            return next(item for item in sockets if item.name == name and item.type == "RGBA")

        socket(mix.inputs, "B").default_value = (0.42, 0.39, 0.35, 1.0)
        links.new(ramp.outputs["Result"], next(item for item in mix.inputs if item.name == "Factor" and item.type == "VALUE"))
        links.new(base.outputs["Color"], socket(mix.inputs, "A"))
        links.new(socket(mix.outputs, "Result"), bsdf.inputs["Base Color"])
    else:
        links.new(base.outputs["Color"], bsdf.inputs["Base Color"])
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
    layer = me.uv_layers.new(name="ImageUV")
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


def oriented_box(name, center, size, axes, material, bone, bevel=0.0):
    """axes=(x축, y축, z축) 단위 벡터. size는 그 축 방향 치수."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    ax, ay, az = (Vector(a).normalized() for a in axes)
    rot = Matrix((ax, ay, az)).transposed()
    for v in bm.verts:
        local = Vector((v.co.x * size[0], v.co.y * size[1], v.co.z * size[2]))
        v.co = Vector(center) + rot @ local
    if bevel > 0.0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=2, affect="EDGES", profile=0.5)
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
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = ig._link(bpy.data.objects.new(name, me))
    ig.assign_material(ob, material)
    triplanar_uv(ob)
    a = ob.vertex_groups.new(name=bones[0])
    b = ob.vertex_groups.new(name=bones[1])
    axis = p1 - p0
    length2 = max(axis.length_squared, 1e-6)
    for v in ob.data.vertices:
        t = max(0.0, min(1.0, (v.co - p0).dot(axis) / length2))
        w = t * t * (3.0 - 2.0 * t)
        a.add([v.index], 1.0 - w, "REPLACE")
        b.add([v.index], w, "REPLACE")
    return ob


def bead(name, points, radius, material, bone):
    ob = ig.pipe(name, [Vector(p) for p in points], radius, resolution=6)
    ig.assign_material(ob, material)
    triplanar_uv(ob)
    group = ob.vertex_groups.new(name=bone)
    group.add(list(range(len(ob.data.vertices))), 1.0, "REPLACE")
    return ob


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


def build_parts(mats):
    parts = []
    foam, membrane, steel, silicone, glue, iron = (mats[k] for k in ("foam", "membrane", "steel", "silicone", "glue", "iron"))
    X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
    # 누르는 판(몸의 중심). 방수막으로 싼 어두운 판이고, 바닥 테두리에 실리콘 줄을 둘렀다.
    press = ((0.0, 0.0, 0.065), (BODY_X, BODY_Y, 0.13))
    parts.append(oriented_box("PressPlate", press[0], press[1], (X, Y, Z), membrane, "body", bevel=0.016))
    hx, hy = BODY_X * 0.5 - 0.012, BODY_Y * 0.5 - 0.012
    rim = [(hx, hy), (-hx, hy), (-hx, -hy), (hx, -hy), (hx, hy)]
    parts.append(bead("SealRim", [(x, y, 0.012) for x, y in rim], 0.016, silicone, "body"))
    # 그 위에 얹힌 방음재 등 두 겹. 조금씩 작고 어긋나 있다(덧대고 또 덧댄 것).
    slabs = [((0.015, -0.02, 0.195), (BODY_X - 0.07, BODY_Y - 0.10, 0.13)),
             ((-0.025, 0.035, 0.31), (BODY_X - 0.20, BODY_Y - 0.26, 0.10))]
    for i, (c, s_) in enumerate(slabs):
        parts.append(oriented_box(f"Slab{i}", c, s_, (X, Y, Z), foam, "body", bevel=0.014))
    # 등에 붙인 방수막 조각 둘(감지 않고 붙였다).
    for i, (c, s_) in enumerate((((0.12, -0.24, 0.362), (0.22, 0.30, 0.008)), ((-0.16, 0.27, 0.362), (0.18, 0.20, 0.008)))):
        parts.append(oriented_box(f"Patch{i}", c, s_, (X, Y, Z), membrane, "body"))
    # 판 가장자리에서 늘어진 방수막 자락 둘.
    for i, (x, sy) in enumerate(((0.16, -1.0), (-0.20, 1.0))):
        c = (x, sy * (BODY_Y * 0.5 + 0.010), 0.10)
        parts.append(oriented_box(f"Flap{i}", c, (0.22, 0.008, 0.16), (X, Y, Z), membrane, "body"))
    # 방음재 등 모서리에 짜 넣은 실리콘 줄(앞쪽 한 변씩).
    for i, (c, s_) in enumerate(slabs):
        hx2, hy2, top = s_[0] * 0.5, s_[1] * 0.5, c[2] + s_[2] * 0.5
        wobble = 0.006 * (1 if i % 2 else -1)
        parts.append(bead(f"BeadA{i}", [(c[0] - hx2, c[1] - hy2, top), (c[0], c[1] - hy2 + wobble, top + 0.004), (c[0] + hx2, c[1] - hy2, top)], 0.010, silicone, "body"))
    # 철골 틀: 등뼈 하나(앞뒤로 길게)와, 누르는 판과 방음재 사이에 깔린 엉덩이 줄의 가로대 둘.
    # 가로대를 방음재 앞에 세우면 노란 등에 줄이 또 그어진다. 판 위에 눕혀 끝만 보이게 한다.
    parts.append(c_channel("Spine", (0.0, -0.62, 0.38), (0.0, 0.62, 0.38), (0, 0, -1), 0.10, 0.05, 0.006, steel, "body"))
    for i, sy in enumerate((-1.0, 1.0)):
        y = HIP_Y * sy
        parts.append(c_channel(f"HipBar{i}", (-HIP_X - 0.06, y, 0.135), (HIP_X + 0.06, y, 0.135), (0, 0, 1), 0.09, 0.04, 0.006, steel, "body"))
    for sx in (1.0, -1.0):
        for sy in (1.0, -1.0):
            # 엉덩이 볼트 받침판.
            hip = Vector((HIP_X * sx, HIP_Y * sy, HIP_Z))
            parts.append(oriented_box(f"HipPlate_{int(sx)}_{int(sy)}", hip + Vector((0.0, -0.035 * sy, 0.0)),
                                      (0.16, 0.012, 0.18), (X, Y, Z), steel, "body"))
    for key, (sx, sy) in LEGS.items():
        hip, knee, ankle, pad = leg_points(sx, sy)
        d = (knee - hip).normalized()
        joint_axis = Vector((-d.y, d.x, 0.0))
        b1, b2, b3 = f"leg_{key}_1", f"leg_{key}_2", f"leg_{key}_3"
        # 바깥 마디가 안쪽 마디를 품는다(접자). 무릎에서 6 cm 겹친다.
        parts.append(c_channel(f"{key}_Seg1", hip - d * 0.05, knee + d * 0.07, (0, 0, 1), 0.135, 0.062, 0.007, steel, b1))
        parts.append(c_channel(f"{key}_Seg2", knee - d * 0.07, ankle + d * 0.06, (0, 0, -1), 0.118, 0.052, 0.007, steel, b2))
        seg3_dir = (pad - ankle).normalized()
        parts.append(c_channel(f"{key}_Seg3", ankle - seg3_dir * 0.04, pad + seg3_dir * 0.02, (d.x, d.y, 0.3), 0.10, 0.045, 0.007, steel, b3))
        parts.append(bolt(f"{key}_HipBolt", hip, joint_axis, 0.020, 0.17, iron, b1))
        parts.append(bolt(f"{key}_KneeBolt", knee, joint_axis, 0.019, 0.16, iron, b2))
        parts.append(bolt(f"{key}_AnkleBolt", ankle, joint_axis, 0.017, 0.13, iron, b3))
        # 패드: 방음재 심을 방수막으로 싸고 가장자리에 실리콘을 둘렀다. 아랫면이 짚는 면이다.
        side = Vector((-d.y, d.x, 0.0))
        parts.append(oriented_box(f"{key}_PadCore", pad + Vector((0.0, 0.0, 0.055)), (0.46, 0.36, 0.11), (d, side, Z), foam, b3, bevel=0.014))
        parts.append(oriented_box(f"{key}_PadWrap", pad + Vector((0.0, 0.0, 0.03)), (0.48, 0.38, 0.06), (d, side, Z), membrane, b3, bevel=0.01))
        rim = [pad + d * 0.23 + side * 0.18, pad - d * 0.23 + side * 0.18, pad - d * 0.23 - side * 0.18, pad + d * 0.23 - side * 0.18]
        parts.append(bead(f"{key}_PadRim", [p + Vector((0, 0, 0.062)) for p in rim + rim[:1]], 0.012, silicone, b3))
        # 몸과 첫 마디 사이에 늘어진 검은 접착층 실 하나, 무릎에 하나.
        for i, (t, off, sag, width) in enumerate(((0.36, 0.06, 0.09, 0.07),)):
            p0 = Vector((HIP_X * sx * 0.85, HIP_Y * sy * 0.96 + off * sy * 0.3, 0.30 - 0.05 * i))
            p1 = hip + (knee - hip) * t + Vector((0.0, 0.0, 0.04))
            parts.append(strand(f"{key}_Glue{i}", p0, p1, sag, width, glue, ("body", b1)))
        for i, (t0, t1, width) in enumerate(((0.78, 0.22, 0.05),)):
            p0 = hip + (knee - hip) * t0 + Vector((0.0, 0.0, 0.035))
            p1 = knee + (ankle - knee) * t1 + Vector((0.0, 0.0, 0.03))
            parts.append(strand(f"{key}_KneeGlue{i}", p0, p1, 0.07, width, glue, (b1, b2)))
    return parts


# --------------------------------------------------------------------------
# 뼈와 자세
# --------------------------------------------------------------------------

def bone_points():
    """UE 좌표의 뼈 머리·꼬리·부모."""
    spec = [("root", Vector((0, 0, 0)), Vector((0, 0, 0.10)), None),
            ("body", Vector((0, 0, HIP_Z)), Vector((0.30, 0, HIP_Z)), "root")]
    for key, (sx, sy) in LEGS.items():
        hip, knee, ankle, pad = leg_points(sx, sy)
        spec.append((f"leg_{key}_1", hip, knee, "body"))
        spec.append((f"leg_{key}_2", knee, ankle, f"leg_{key}_1"))
        spec.append((f"leg_{key}_3", ankle, pad, f"leg_{key}_2"))
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


def pose_legs(arm, body_offset, targets):
    """미리보기용 자세. targets[key] = (패드 가운데, 짚은 면의 바깥 법선, 무릎이 갈 쪽)이면 무릎을
    두 마디 IK로 풀고, ("joints", 무릎, 발목, 패드)면 그 자리에 그대로 놓는다. 좌표는 UE."""
    pb = arm.pose.bones
    arm_mat = {}
    body_rest = arm.data.bones["body"].matrix_local.copy()
    body_mat = Matrix.Translation(mirrored(body_offset)) @ body_rest
    pb["body"].matrix = body_mat
    bpy.context.view_layer.update()
    for key, (sx, sy) in LEGS.items():
        hip0, knee0, ankle0, pad0 = leg_points(sx, sy)
        hip = hip0 + body_offset
        if targets[key][0] == "joints":
            knee, ankle, pad = (p + body_offset for p in targets[key][1:])
        else:
            pad, normal, pole = targets[key]
            seg3 = (pad0 - ankle0).length
            ankle = pad + normal.normalized() * seg3
            l1 = (knee0 - hip0).length
            l2 = (ankle0 - knee0).length
            knee = two_bone_knee(hip, ankle, l1, l2, pole)
        for bname, head, tail in ((f"leg_{key}_1", hip, knee), (f"leg_{key}_2", knee, ankle), (f"leg_{key}_3", ankle, pad)):
            rest = arm.data.bones[bname].matrix_local
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
    return arm_mat


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


def main():
    args = parse_args()
    out_root = os.path.abspath(args.out) if args.out else os.path.join(SOURCE_ART, "Blender")
    name = args.name
    out_dir = os.path.join(out_root, name)
    os.makedirs(out_dir, exist_ok=True)
    ig.reset_scene()
    mats = {
        "foam": tiled_material("LiningFoam", "T_LiningFoam", 0.60, normal_strength=1.2, contact_grime=0.55),
        "membrane": tiled_material("LiningMembrane", "T_LiningMembrane", 1.0, normal_strength=0.8, contact_grime=0.35),
        "steel": tiled_material("LiningSteel", "T_LiningSteel", 0.40, metallic=0.30, normal_strength=0.6, rough_scale=1.2),
        "silicone": flat_material("LiningSilicone", (0.50, 0.50, 0.47), 0.42),
        "glue": flat_material("LiningGlue", (0.012, 0.011, 0.010), 0.22),
        "iron": flat_material("LiningBolt", (0.30, 0.29, 0.27), 0.38, metallic=0.9),
    }
    parts = build_parts(mats)
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
    for poly in body.data.polygons:
        poly.use_smooth = False
    lo, hi = ig.bounds(body)
    ig.log(f"{name}: tris={ig.triangle_count(body)} bounds {(hi - lo).x:.2f} x {(hi - lo).y:.2f} x {(hi - lo).z:.2f} m")
    ue_bounds = (lo.copy(), hi.copy())
    ig.render_preview(body, os.path.join(out_dir, f"{name}_source_preview.png"), camera_yaw_deg=-35.0, camera_pitch_deg=28.0)

    ig.mirror_y(body)
    textures = {}
    if not args.no_bake:
        ig.uv_smart(body, margin=0.003)
        textures = ig.bake_textures(body, name, out_dir, size=args.texture_size)
        ig.preview_material_from_bakes(body, textures)
    # 굽기가 끝나면 타일 UV는 버린다. UE는 첫 UV 채널을 구운 텍스처에 쓴다.
    if "ImageUV" in body.data.uv_layers:
        body.data.uv_layers.remove(body.data.uv_layers["ImageUV"])

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
    pose_legs(arm, Vector((0.0, 0.0, 1.35)), spread_targets())
    ig.render_preview(body, os.path.join(out_dir, f"{name}_spread.png"), camera_yaw_deg=-75.0, camera_pitch_deg=6.0)
    clear_pose(arm)

    if args.no_bake:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, f"{name}.blend"))
        ig.log(f"{name}: previews only (--no-bake)")
        return

    slots = ig.finalize_slots(body)
    fbx_path = os.path.join(out_dir, f"{name}.fbx")
    export_rig_fbx(fbx_path, arm, body)
    ig.write_manifest(out_dir, name, "hero", fbx_path, textures, body, slots,
                      notes="덧댄 것. 절차 모델(rig_lining.py), 뼈 열넷, 동작 없음(게임이 뼈를 놓는다)",
                      origin="body-bottom-center", ue_bounds=ue_bounds)
    manifest_path = os.path.join(out_dir, "manifest.json")
    with open(manifest_path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    data["skeletal"] = True
    data["animations"] = {}
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
