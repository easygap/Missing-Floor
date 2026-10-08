"""Blender 헤드리스 에셋 제작 공용 라이브러리.

지오메트리 스크립트로 상자를 쌓던 소품을 Blender에서 다시 만든다. 여기 있는
것은 빌더 스크립트(build_*.py)가 공통으로 쓰는 것들이다.

- 미터 단위 원시 도형(상자·원기둥·라뜨·파이프)과 베벨·불리언·결합
- 각도 기준 샤프 에지, 스마트 UV
- 절차 PBR 재질(도장 강판·스테인리스·플라스틱·고무·유리·발광)
- Cycles로 BaseColor / Normal(DX) / ORM / Emissive 굽기
- UCX 충돌 껍데기, FBX 내보내기, EEVEE 미리보기 렌더
- import_blender_assets.py가 읽는 manifest.json

좌표는 UE와 같게 둔다. 빌더는 UE의 X, Y, Z(cm를 m로)로 생각하고 그린다.
build_probe_axes.py로 실측한 결과 기본 FBX 경로에서 X·Z는 그대로고 Y만
뒤집힌다(Blender +0.35 → UE -35). 그래서 build_asset이 결합 직후 정점의 Y를
거울 반전하고 면을 뒤집는다. UE가 임포트하며 다시 뒤집으므로 빌더가 적은
좌표가 그대로 UE 좌표가 된다. 굽기는 반전 뒤에 하므로 노멀맵도 UE가 보는
기하 그대로다.

실행은 항상 이렇게 한다.

    blender -b --factory-startup --python Scripts/blender/build_xxx.py -- <out_dir>
"""

from __future__ import annotations

import json
import math
import os
import sys
import time

import bpy
import bmesh
from mathutils import Matrix, Vector

# --------------------------------------------------------------------------
# 상수
# --------------------------------------------------------------------------

# UE와 축을 맞추기 위한 FBX 내보내기 설정. build_probe_axes.py로 실측했다.
EXPORT_AXIS = {"axis_forward": "-Z", "axis_up": "Y"}

TEXTURE_SIZE = {"hero": 2048, "prop": 1024, "large": 2048}

# 이미지 굽기 샘플 수. 색·거칠기·노멀은 1이면 되고 AO만 샘플이 필요하다.
AO_SAMPLES = 24


def log(message: str) -> None:
    print(f"[IGBL] {message}", flush=True)


# --------------------------------------------------------------------------
# 씬
# --------------------------------------------------------------------------

def reset_scene() -> None:
    """빈 씬. 단위는 미터, 배율 1."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.unit_settings.length_unit = "METERS"


def _link(ob: bpy.types.Object) -> bpy.types.Object:
    bpy.context.scene.collection.objects.link(ob)
    return ob


def _mesh_from_bm(name: str, bm: bmesh.types.BMesh) -> bpy.types.Mesh:
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.update()
    return me


def set_active(ob: bpy.types.Object, solo: bool = True) -> None:
    # 지운 오브젝트가 뷰 레이어 목록에 None으로 남아 있을 수 있다.
    bpy.context.view_layer.update()
    if solo:
        for other in list(bpy.context.view_layer.objects):
            if other is not None:
                other.select_set(False)
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob


# --------------------------------------------------------------------------
# 원시 도형 (전부 미터, 로컬 원점은 인자로)
# --------------------------------------------------------------------------

def box(name, size, location=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0),
        origin="center", bevel=None, segments=2, material=None):
    """상자. origin은 center 또는 bottom(바닥 중심)."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=Vector(size), verts=bm.verts)
    if origin == "bottom":
        bmesh.ops.translate(bm, vec=Vector((0.0, 0.0, size[2] * 0.5)), verts=bm.verts)
    ob = _link(bpy.data.objects.new(name, _mesh_from_bm(name, bm)))
    ob.location = location
    ob.rotation_euler = rotation
    if bevel:
        add_bevel(ob, bevel, segments)
    if material is not None:
        assign_material(ob, material)
    return ob


def cylinder(name, radius, depth, location=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0),
             segments=32, origin="center", bevel=None, bevel_segments=2,
             radius_top=None, material=None, cap=True):
    """원기둥(또는 원뿔대). 축은 로컬 Z."""
    bm = bmesh.new()
    top = radius if radius_top is None else radius_top
    bmesh.ops.create_cone(
        bm, cap_ends=cap, cap_tris=False, segments=segments,
        radius1=radius, radius2=top, depth=depth)
    if origin == "bottom":
        bmesh.ops.translate(bm, vec=Vector((0.0, 0.0, depth * 0.5)), verts=bm.verts)
    ob = _link(bpy.data.objects.new(name, _mesh_from_bm(name, bm)))
    ob.location = location
    ob.rotation_euler = rotation
    if bevel:
        add_bevel(ob, bevel, bevel_segments)
    if material is not None:
        assign_material(ob, material)
    return ob


def lathe(name, profile, segments=48, location=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0),
          material=None):
    """(반지름, 높이) 프로파일을 Z축으로 회전시킨다. 병·손잡이·둥근 등에 쓴다.

    프로파일은 아래에서 위로. 첫 점과 끝 점의 반지름이 0이면 닫힌 형상이 된다.
    """
    bm = bmesh.new()
    verts = [bm.verts.new((r, 0.0, z)) for r, z in profile]
    edges = [bm.edges.new((verts[i], verts[i + 1])) for i in range(len(verts) - 1)]
    geom = verts + edges
    bmesh.ops.spin(
        bm, geom=geom, cent=(0.0, 0.0, 0.0), axis=(0.0, 0.0, 1.0),
        dvec=(0.0, 0.0, 0.0), angle=math.tau, steps=segments, use_merge=True)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    ob = _link(bpy.data.objects.new(name, _mesh_from_bm(name, bm)))
    ob.location = location
    ob.rotation_euler = rotation
    if material is not None:
        assign_material(ob, material)
    return ob


def rounded_polyline(points, radius, steps=6):
    """꺾이는 자리를 호로 다듬은 점 목록. 파이프 경로에 쓴다."""
    pts = [Vector(p) for p in points]
    if len(pts) < 3 or radius <= 0.0:
        return pts
    out = [pts[0]]
    for i in range(1, len(pts) - 1):
        prev, cur, nxt = pts[i - 1], pts[i], pts[i + 1]
        d0 = (prev - cur)
        d1 = (nxt - cur)
        l0, l1 = d0.length, d1.length
        d0.normalize()
        d1.normalize()
        r = min(radius, l0 * 0.49, l1 * 0.49)
        a = cur + d0 * r
        b = cur + d1 * r
        for s in range(steps + 1):
            t = s / steps
            # 2차 베지어로 모서리를 둥글린다.
            out.append(a * (1 - t) ** 2 + cur * 2 * (1 - t) * t + b * t ** 2)
    out.append(pts[-1])
    return out


def pipe(name, points, radius, resolution=8, corner_radius=0.0, corner_steps=6,
         caps=True, material=None):
    """점 목록을 따라가는 관. 손잡이·배관·호스."""
    pts = rounded_polyline(points, corner_radius, corner_steps) if corner_radius > 0 else [Vector(p) for p in points]
    curve = bpy.data.curves.new(name + "_curve", "CURVE")
    curve.dimensions = "3D"
    curve.bevel_depth = radius
    curve.bevel_resolution = max(1, resolution // 2 - 1)
    curve.use_fill_caps = caps
    spline = curve.splines.new("POLY")
    spline.points.add(len(pts) - 1)
    for i, p in enumerate(pts):
        spline.points[i].co = (p.x, p.y, p.z, 1.0)
    spline.use_smooth = True
    curve_ob = _link(bpy.data.objects.new(name + "_curve", curve))
    depsgraph = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(curve_ob.evaluated_get(depsgraph))
    me.name = name
    bpy.data.objects.remove(curve_ob, do_unlink=True)
    bpy.data.curves.remove(curve)
    ob = _link(bpy.data.objects.new(name, me))
    if material is not None:
        assign_material(ob, material)
    return ob


def torus(name, major_radius, minor_radius, location=(0.0, 0.0, 0.0),
          rotation=(0.0, 0.0, 0.0), major_segments=32, minor_segments=12, material=None):
    profile_pts = []
    for i in range(minor_segments + 1):
        a = math.tau * i / minor_segments
        profile_pts.append((major_radius + minor_radius * math.cos(a), minor_radius * math.sin(a)))
    return lathe(name, profile_pts, segments=major_segments, location=location,
                 rotation=rotation, material=material)


# --------------------------------------------------------------------------
# 편집
# --------------------------------------------------------------------------

def add_bevel(ob, width, segments=2, angle_deg=30.0, harden=True):
    mod = ob.modifiers.new("Bevel", "BEVEL")
    mod.width = width
    mod.segments = segments
    mod.limit_method = "ANGLE"
    mod.angle_limit = math.radians(angle_deg)
    mod.harden_normals = harden
    mod.miter_outer = "MITER_ARC"
    mod.use_clamp_overlap = True
    return mod


def apply_modifiers(ob):
    """모디파이어를 전부 굽는다. 평가 결과를 메시로 바꿔 끼우는 방식이라
    선택 상태나 컨텍스트에 기대지 않는다."""
    if not ob.modifiers:
        return ob
    depsgraph = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(depsgraph))
    old = ob.data
    me.name = old.name
    ob.modifiers.clear()
    ob.data = me
    if old.users == 0:
        bpy.data.meshes.remove(old)
    return ob


def boolean(target, cutter, operation="DIFFERENCE", remove_cutter=True):
    mod = target.modifiers.new("Boolean", "BOOLEAN")
    mod.operation = operation
    mod.object = cutter
    mod.solver = "EXACT"
    apply_modifiers(target)
    if remove_cutter:
        me = cutter.data
        bpy.data.objects.remove(cutter, do_unlink=True)
        if me.users == 0:
            bpy.data.meshes.remove(me)
    return target


def _world_bm(ob):
    """오브젝트의 변환을 정점에 구워 넣은 bmesh 사본."""
    # location만 넣고 바로 읽으면 matrix_world는 아직 단위행렬이다. 갱신을
    # 안 하면 부품이 전부 원점으로 모인다 — 프로브가 그렇게 나왔었다.
    bpy.context.view_layer.update()
    apply_modifiers(ob)
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.transform(bm, matrix=ob.matrix_world, verts=bm.verts)
    if ob.matrix_world.determinant() < 0.0:
        bmesh.ops.reverse_faces(bm, faces=bm.faces)
    return bm


def join(objects, name):
    """여러 오브젝트를 하나로. 재질 슬롯은 이름 기준으로 합친다."""
    materials = []
    for ob in objects:
        for slot in ob.material_slots:
            if slot.material is not None and slot.material not in materials:
                materials.append(slot.material)
    bm = bmesh.new()
    for ob in objects:
        remap = {}
        for index, slot in enumerate(ob.material_slots):
            remap[index] = materials.index(slot.material) if slot.material in materials else 0
        part = _world_bm(ob)
        for face in part.faces:
            face.material_index = remap.get(face.material_index, 0)
        temp = bpy.data.meshes.new("__join_tmp")
        part.to_mesh(temp)
        part.free()
        bm.from_mesh(temp)
        bpy.data.meshes.remove(temp)
    me = _mesh_from_bm(name, bm)
    for mat in materials:
        me.materials.append(mat)
    # 활성 색 속성이 풀리면 Color Attribute 노드가 첫 속성(정수·불리언)을
    # 색으로 읽어 흑백 얼룩이 된다. 이름이 있는 색 속성을 활성으로 되돌린다.
    if me.color_attributes:
        me.color_attributes.active_color = me.color_attributes[0]
        me.color_attributes.render_color_index = 0
    joined = _link(bpy.data.objects.new(name, me))
    for ob in objects:
        old = ob.data
        bpy.data.objects.remove(ob, do_unlink=True)
        if old.users == 0:
            bpy.data.meshes.remove(old)
    return joined


def set_origin(ob, point):
    """원점을 월드 좌표 point로 옮긴다(정점은 제자리)."""
    bpy.context.view_layer.update()
    offset = Vector(point) - ob.matrix_world.translation
    inv = ob.matrix_world.to_3x3().inverted()
    local = inv @ offset
    for v in ob.data.vertices:
        v.co -= local
    ob.location = Vector(point)


def mirror_y(ob):
    """정점 Y를 뒤집고 면 방향을 되돌린다. UE 임포트의 Y 반전을 상쇄한다."""
    me = ob.data
    for v in me.vertices:
        v.co.y = -v.co.y
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.reverse_faces(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    me.update()


def mark_sharp_by_angle(ob, angle_deg=30.0):
    """모든 면을 스무스로 두고, 꺾임이 큰 에지만 샤프로 찍는다.
    FBX가 이 샤프 에지를 스무딩 정보로 들고 가서 UE 노멀이 이걸 따른다."""
    me = ob.data
    for poly in me.polygons:
        poly.use_smooth = True
    bm = bmesh.new()
    bm.from_mesh(me)
    threshold = math.radians(angle_deg)
    for edge in bm.edges:
        if len(edge.link_faces) == 2:
            try:
                edge.smooth = edge.calc_face_angle() < threshold
            except ValueError:
                edge.smooth = True
        else:
            edge.smooth = False
    bm.to_mesh(me)
    bm.free()
    me.update()


def uv_smart(ob, angle_deg=66.0, margin=0.004):
    """스마트 UV. 헤드리스에서도 편집 모드 연산은 활성 오브젝트만 있으면 돈다."""
    set_active(ob)
    me = ob.data
    if "UVMap" not in me.uv_layers:
        me.uv_layers.new(name="UVMap")
    me.uv_layers.active = me.uv_layers["UVMap"]
    me.uv_layers["UVMap"].active_render = True
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(
        angle_limit=math.radians(angle_deg), island_margin=margin,
        # 결과는 정사각형 베이크다. 원본 라벨(예: 2048×512)의 종횡비로
        # UV를 보정하면 섬이 세로로 눌리고 작은 글자가 번진다.
        area_weight=0.0, correct_aspect=False, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode="OBJECT")


def uv_box_project(ob, scale=1.0):
    """면 노멀이 향하는 축으로 투영하는 단순 박스 UV. 타일 재질용."""
    me = ob.data
    if not me.uv_layers:
        me.uv_layers.new(name="UVMap")
    uv = me.uv_layers.active.data
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
            uv[li].uv = (u * scale, v * scale)


def triangle_count(ob) -> int:
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)


def bounds(ob):
    """월드 AABB (min, max)."""
    bpy.context.view_layer.update()
    pts = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    return lo, hi


# --------------------------------------------------------------------------
# 재질
# --------------------------------------------------------------------------

def assign_material(ob, material):
    me = ob.data
    if material.name not in [m.name for m in me.materials if m]:
        me.materials.append(material)
    index = [m.name if m else None for m in me.materials].index(material.name)
    for poly in me.polygons:
        poly.material_index = index


def _nodes(material):
    material.use_nodes = True
    tree = material.node_tree
    return tree, tree.nodes, tree.links


def _principled(material):
    tree, nodes, links = _nodes(material)
    bsdf = nodes.get("Principled BSDF")
    if bsdf is None:
        bsdf = nodes.new("ShaderNodeBsdfPrincipled")
        out = nodes.get("Material Output") or nodes.new("ShaderNodeOutputMaterial")
        links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    return bsdf


def _noise(nodes, links, scale, detail=2.0, roughness=0.5, coords=None, distortion=0.0):
    n = nodes.new("ShaderNodeTexNoise")
    n.inputs["Scale"].default_value = scale
    n.inputs["Detail"].default_value = detail
    n.inputs["Roughness"].default_value = roughness
    n.inputs["Distortion"].default_value = distortion
    if coords is not None:
        links.new(coords, n.inputs["Vector"])
    return n


def _map_range(nodes, links, value_socket, from_min, from_max, to_min, to_max):
    m = nodes.new("ShaderNodeMapRange")
    m.inputs["From Min"].default_value = from_min
    m.inputs["From Max"].default_value = from_max
    m.inputs["To Min"].default_value = to_min
    m.inputs["To Max"].default_value = to_max
    links.new(value_socket, m.inputs["Value"])
    return m


def _object_coords(nodes):
    tc = nodes.new("ShaderNodeTexCoord")
    return tc.outputs["Object"]


def mat_painted_steel(name, color, roughness=0.55, wear=0.25, bump=0.10, dirt_color=None):
    """무광 도장 강판. 오렌지필 요철, 거칠기 얼룩, 바닥 쪽으로 진해지는 때."""
    mat = bpy.data.materials.new(name)
    bsdf = _principled(mat)
    tree, nodes, links = _nodes(mat)
    coords = _object_coords(nodes)
    # 색 얼룩: 큰 노이즈로 미세한 톤 변화, 바닥 근처에 때.
    tone = _noise(nodes, links, 3.0, detail=3.0, coords=coords)
    tone_range = _map_range(nodes, links, tone.outputs["Fac"], 0.35, 0.65, 0.92, 1.06)
    base = nodes.new("ShaderNodeRGB")
    base.outputs[0].default_value = (*color, 1.0)
    tinted = nodes.new("ShaderNodeMixRGB")
    tinted.blend_type = "MULTIPLY"
    tinted.inputs["Fac"].default_value = 1.0
    links.new(base.outputs[0], tinted.inputs["Color1"])
    links.new(tone_range.outputs["Result"], tinted.inputs["Color2"])
    dirt = dirt_color or (color[0] * 0.55, color[1] * 0.52, color[2] * 0.48)
    dirt_rgb = nodes.new("ShaderNodeRGB")
    dirt_rgb.outputs[0].default_value = (*dirt, 1.0)
    # 높이(Object Z)가 낮을수록 때가 많다. 0~0.35 m 구간.
    sep = nodes.new("ShaderNodeSeparateXYZ")
    links.new(coords, sep.inputs["Vector"])
    height = _map_range(nodes, links, sep.outputs["Z"], 0.0, 0.35, 1.0, 0.0)
    grime = _noise(nodes, links, 9.0, detail=4.0, roughness=0.7, coords=coords)
    grime_amt = nodes.new("ShaderNodeMath")
    grime_amt.operation = "MULTIPLY"
    links.new(height.outputs["Result"], grime_amt.inputs[0])
    links.new(grime.outputs["Fac"], grime_amt.inputs[1])
    grime_scaled = nodes.new("ShaderNodeMath")
    grime_scaled.operation = "MULTIPLY"
    grime_scaled.inputs[1].default_value = wear
    links.new(grime_amt.outputs[0], grime_scaled.inputs[0])
    final = nodes.new("ShaderNodeMixRGB")
    links.new(grime_scaled.outputs[0], final.inputs["Fac"])
    links.new(tinted.outputs[0], final.inputs["Color1"])
    links.new(dirt_rgb.outputs[0], final.inputs["Color2"])
    links.new(final.outputs[0], bsdf.inputs["Base Color"])
    # 거칠기: 기본값 주변으로 얼룩, 때 있는 곳은 더 거칠게.
    rough_noise = _noise(nodes, links, 14.0, detail=3.0, coords=coords)
    rough_range = _map_range(nodes, links, rough_noise.outputs["Fac"], 0.3, 0.7,
                             roughness - 0.08, roughness + 0.08)
    rough_final = nodes.new("ShaderNodeMath")
    rough_final.operation = "ADD"
    links.new(rough_range.outputs["Result"], rough_final.inputs[0])
    rough_dirt = nodes.new("ShaderNodeMath")
    rough_dirt.operation = "MULTIPLY"
    rough_dirt.inputs[1].default_value = 0.25
    links.new(grime_scaled.outputs[0], rough_dirt.inputs[0])
    links.new(rough_dirt.outputs[0], rough_final.inputs[1])
    links.new(rough_final.outputs[0], bsdf.inputs["Roughness"])
    bsdf.inputs["Metallic"].default_value = 0.0
    bsdf.inputs["Specular IOR Level"].default_value = 0.5
    # 오렌지필: 잔 노이즈 범프.
    peel = _noise(nodes, links, 220.0, detail=2.0, coords=coords)
    bumpn = nodes.new("ShaderNodeBump")
    bumpn.inputs["Strength"].default_value = bump
    bumpn.inputs["Distance"].default_value = 0.002
    links.new(peel.outputs["Fac"], bumpn.inputs["Height"])
    links.new(bumpn.outputs["Normal"], bsdf.inputs["Normal"])
    mat["ig_kind"] = "baked"
    return mat


def mat_metal(name, color=(0.78, 0.78, 0.76), roughness=0.32, streak=0.12, anisotropic=True):
    """스테인리스·아연도금. 가로 결 노이즈로 헤어라인을 흉내 낸다."""
    mat = bpy.data.materials.new(name)
    bsdf = _principled(mat)
    tree, nodes, links = _nodes(mat)
    coords = _object_coords(nodes)
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Metallic"].default_value = 1.0
    # 결: 한 축으로 길게 늘인 노이즈.
    mapping = nodes.new("ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (1.0, 60.0, 60.0) if anisotropic else (1.0, 1.0, 1.0)
    links.new(coords, mapping.inputs["Vector"])
    streaks = _noise(nodes, links, 40.0, detail=2.0, coords=mapping.outputs["Vector"])
    rough_range = _map_range(nodes, links, streaks.outputs["Fac"], 0.3, 0.7,
                             roughness - streak, roughness + streak)
    smudge = _noise(nodes, links, 6.0, detail=2.0, coords=coords)
    smudge_range = _map_range(nodes, links, smudge.outputs["Fac"], 0.4, 0.6, -0.05, 0.10)
    rough_final = nodes.new("ShaderNodeMath")
    rough_final.operation = "ADD"
    links.new(rough_range.outputs["Result"], rough_final.inputs[0])
    links.new(smudge_range.outputs["Result"], rough_final.inputs[1])
    links.new(rough_final.outputs[0], bsdf.inputs["Roughness"])
    bumpn = nodes.new("ShaderNodeBump")
    bumpn.inputs["Strength"].default_value = 0.02
    bumpn.inputs["Distance"].default_value = 0.001
    links.new(streaks.outputs["Fac"], bumpn.inputs["Height"])
    links.new(bumpn.outputs["Normal"], bsdf.inputs["Normal"])
    mat["ig_kind"] = "baked"
    return mat


def mat_plastic(name, color, roughness=0.5, bump=0.015, grain_scale=300.0):
    """사출 플라스틱. 잔 입자 요철과 약한 거칠기 변화."""
    mat = bpy.data.materials.new(name)
    bsdf = _principled(mat)
    tree, nodes, links = _nodes(mat)
    coords = _object_coords(nodes)
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Metallic"].default_value = 0.0
    grain = _noise(nodes, links, grain_scale, detail=1.0, coords=coords)
    rough_range = _map_range(nodes, links, grain.outputs["Fac"], 0.3, 0.7,
                             roughness - 0.05, roughness + 0.05)
    links.new(rough_range.outputs["Result"], bsdf.inputs["Roughness"])
    if bump > 0.0:
        bumpn = nodes.new("ShaderNodeBump")
        bumpn.inputs["Strength"].default_value = bump
        bumpn.inputs["Distance"].default_value = 0.001
        links.new(grain.outputs["Fac"], bumpn.inputs["Height"])
        links.new(bumpn.outputs["Normal"], bsdf.inputs["Normal"])
    mat["ig_kind"] = "baked"
    return mat


def mat_rubber(name, color=(0.03, 0.03, 0.03), roughness=0.82):
    return mat_plastic(name, color, roughness=roughness, bump=0.03, grain_scale=120.0)


def mat_gloss(name, color=(0.01, 0.01, 0.012), roughness=0.12):
    """유광 검정(터치 유리, 도어락 패널)."""
    mat = mat_plastic(name, color, roughness=roughness, bump=0.0)
    return mat


def mat_emissive(name, color, strength=4.0):
    """발광면. Emission만 굽고 BaseColor는 어둡게 둔다."""
    mat = bpy.data.materials.new(name)
    bsdf = _principled(mat)
    bsdf.inputs["Base Color"].default_value = (color[0] * 0.3, color[1] * 0.3, color[2] * 0.3, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.4
    bsdf.inputs["Emission Color"].default_value = (*color, 1.0)
    bsdf.inputs["Emission Strength"].default_value = strength
    mat["ig_kind"] = "baked"
    mat["ig_emissive"] = True
    return mat


def mat_speckle(name, base, speck, scale=900.0, threshold=0.62, roughness=0.32, bump=0.02):
    """인조 대리석·테라조 상판. 잔 보로노이 점을 바탕색 위에 뿌린다."""
    mat = bpy.data.materials.new(name)
    bsdf = _principled(mat)
    tree, nodes, links = _nodes(mat)
    coords = _object_coords(nodes)
    vor = nodes.new("ShaderNodeTexVoronoi")
    vor.inputs["Scale"].default_value = scale
    vor.inputs["Randomness"].default_value = 1.0
    links.new(coords, vor.inputs["Vector"])
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = threshold - 0.06
    ramp.color_ramp.elements[0].color = (0.0, 0.0, 0.0, 1.0)
    ramp.color_ramp.elements[1].position = threshold
    ramp.color_ramp.elements[1].color = (1.0, 1.0, 1.0, 1.0)
    links.new(vor.outputs["Distance"], ramp.inputs["Fac"])
    base_rgb = nodes.new("ShaderNodeRGB")
    base_rgb.outputs[0].default_value = (*base, 1.0)
    speck_rgb = nodes.new("ShaderNodeRGB")
    speck_rgb.outputs[0].default_value = (*speck, 1.0)
    mix = nodes.new("ShaderNodeMixRGB")
    links.new(ramp.outputs["Color"], mix.inputs["Fac"])
    links.new(base_rgb.outputs[0], mix.inputs["Color1"])
    links.new(speck_rgb.outputs[0], mix.inputs["Color2"])
    # 큰 노이즈로 톤이 조금 흔들린다.
    tone = _noise(nodes, links, 2.0, detail=2.0, coords=coords)
    tone_range = _map_range(nodes, links, tone.outputs["Fac"], 0.35, 0.65, 0.9, 1.08)
    tinted = nodes.new("ShaderNodeMixRGB")
    tinted.blend_type = "MULTIPLY"
    tinted.inputs["Fac"].default_value = 1.0
    links.new(mix.outputs[0], tinted.inputs["Color1"])
    links.new(tone_range.outputs["Result"], tinted.inputs["Color2"])
    links.new(tinted.outputs[0], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = roughness
    if bump > 0.0:
        bumpn = nodes.new("ShaderNodeBump")
        bumpn.inputs["Strength"].default_value = bump
        bumpn.inputs["Distance"].default_value = 0.0005
        links.new(ramp.outputs["Color"], bumpn.inputs["Height"])
        links.new(bumpn.outputs["Normal"], bsdf.inputs["Normal"])
    mat["ig_kind"] = "baked"
    return mat


def mat_image_uv(name, image_path, uv_layer="ImageUV", roughness=0.45, metallic=0.0,
                 emission_strength=0.0):
    """이미지 한 장을 별도 UV 층으로 읽는 재질. 스마트 UV가 활성 층을 새로 펴도
    이 층은 그대로라 굽기 때 이미지가 제자리에 들어간다."""
    mat = bpy.data.materials.new(name)
    bsdf = _principled(mat)
    tree, nodes, links = _nodes(mat)
    uv = nodes.new("ShaderNodeUVMap")
    uv.uv_map = uv_layer
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(image_path)
    tex.image.colorspace_settings.name = "sRGB"
    tex.extension = "EXTEND"
    links.new(uv.outputs["UV"], tex.inputs["Vector"])
    links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    if emission_strength > 0.0:
        links.new(tex.outputs["Color"], bsdf.inputs["Emission Color"])
        bsdf.inputs["Emission Strength"].default_value = emission_strength
        mat["ig_emissive"] = True
    mat["ig_kind"] = "baked"
    return mat


def image_quad(name, size, location, material, rotation=(0.0, 0.0, 0.0), uv_layer="ImageUV",
               thickness=0.0008, uv_rect=(0.0, 0.0, 1.0, 1.0)):
    """이미지를 붙일 얇은 판. 앞면(-Y)에 0..1 UV를 ImageUV 층으로 준다.
    size는 (폭 X, 높이 Z). uv_rect=(u0, v0, u1, v1)이면 아틀라스의 그 칸만 쓴다
    (v는 아래가 0, PIL 좌표계와 반대)."""
    ob = box(name, (size[0], thickness, size[1]), location=location, rotation=rotation, material=material)
    me = ob.data
    layer = me.uv_layers.new(name=uv_layer)
    u0, v0, u1, v1 = uv_rect
    for poly in me.polygons:
        n = poly.normal
        for li in poly.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            u = co.x / size[0] + 0.5
            v = co.z / size[1] + 0.5
            if n.y > 0.5:
                u = 1.0 - u
            layer.data[li].uv = (u0 + (u1 - u0) * u, v0 + (v1 - v0) * v)
    return ob


def atlas_rect(columns, rows, index):
    """PIL로 그린 아틀라스(왼쪽 위가 0)의 index번째 칸을 Blender UV 사각형으로."""
    col, row = index % columns, index // columns
    u0, u1 = col / columns, (col + 1) / columns
    v1 = 1.0 - row / rows
    v0 = 1.0 - (row + 1) / rows
    return (u0, v0, u1, v1)


def mat_glass(name="Glass"):
    """UE 쪽 기존 M_Glass 슬롯으로 넘길 유리. 굽지 않는다."""
    mat = bpy.data.materials.new(name)
    bsdf = _principled(mat)
    bsdf.inputs["Base Color"].default_value = (0.9, 0.95, 1.0, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.05
    bsdf.inputs["Transmission Weight"].default_value = 1.0
    mat["ig_kind"] = "glass"
    return mat


def mat_image(name, image_path, roughness=0.5, metallic=0.0, colorspace="sRGB"):
    """이미지 한 장을 알베도로 쓰는 재질(생성 시트 조각·라벨)."""
    mat = bpy.data.materials.new(name)
    bsdf = _principled(mat)
    tree, nodes, links = _nodes(mat)
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(image_path)
    tex.image.colorspace_settings.name = colorspace
    links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    mat["ig_kind"] = "baked"
    return mat


# --------------------------------------------------------------------------
# 굽기
# --------------------------------------------------------------------------

def _baked_materials(ob):
    return [m for m in ob.data.materials if m is not None and m.get("ig_kind") == "baked"]


def _has_emissive(ob):
    return any(m.get("ig_emissive") for m in _baked_materials(ob))


def _bake_target_nodes(materials, image):
    """굽기 대상 이미지 노드를 각 재질에 하나씩 심고 활성으로 둔다."""
    created = []
    for mat in materials:
        tree, nodes, links = _nodes(mat)
        node = nodes.new("ShaderNodeTexImage")
        node.name = "__ig_bake_target"
        node.image = image
        nodes.active = node
        created.append((mat, node))
    return created


def _remove_bake_nodes(created):
    for mat, node in created:
        mat.node_tree.nodes.remove(node)


def _new_image(name, size, srgb):
    img = bpy.data.images.new(name, size, size, alpha=False, float_buffer=False)
    img.colorspace_settings.name = "sRGB" if srgb else "Non-Color"
    return img


def _bake(ob, bake_type, image, materials, samples=1, pass_filter=None, normal_space="TANGENT",
          source=None, cage_extrusion=0.0, max_ray_distance=0.0, target="IMAGE_TEXTURES"):
    """ob의 재질에 이미지 노드를 심고 굽는다. source를 주면 selected-to-active:
    source(고밀도)의 표면을 ob(저밀도)의 UV로 옮겨 굽는다."""
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = False
    scene.render.bake.margin = 8
    scene.render.bake.use_clear = True
    scene.render.bake.use_selected_to_active = source is not None
    if source is not None:
        scene.render.bake.cage_extrusion = cage_extrusion
        scene.render.bake.max_ray_distance = max_ray_distance
        scene.render.bake.use_cage = False
    set_active(ob)
    if source is not None:
        source.select_set(True)
        bpy.context.view_layer.objects.active = ob
    created = _bake_target_nodes(materials, image) if target == "IMAGE_TEXTURES" else []
    kwargs = {"type": bake_type, "margin": 8, "use_clear": True,
              "use_selected_to_active": source is not None, "target": target}
    if source is not None:
        kwargs["cage_extrusion"] = cage_extrusion
        kwargs["max_ray_distance"] = max_ray_distance
    if pass_filter is not None:
        kwargs["pass_filter"] = pass_filter
    if bake_type == "NORMAL":
        kwargs["normal_space"] = normal_space
    started = time.time()
    bpy.ops.object.bake(**kwargs)
    _remove_bake_nodes(created)
    log(f"  bake {bake_type} {image.size[0] if image is not None else 0}px "
        f"{'from ' + source.name + ' ' if source is not None else ''}{time.time() - started:.1f}s")


def _bake_input_via_emit(ob, image, materials, input_name, default=0.0, source=None,
                         source_materials=None, cage_extrusion=0.0, max_ray_distance=0.0):
    """Principled 입력 하나(Metallic 등)를 발광으로 갈아 끼워 값 그대로 굽는다.
    source가 있으면 그쪽 재질(source_materials)을 갈아 끼우고 ob의 UV로 굽는다."""
    swaps = []
    for mat in (source_materials if source is not None else materials):
        tree, nodes, links = _nodes(mat)
        bsdf = _principled(mat)
        out = nodes.get("Material Output")
        emit = nodes.new("ShaderNodeEmission")
        emit.name = "__ig_emit"
        emit.inputs["Strength"].default_value = 1.0
        socket = bsdf.inputs[input_name]
        if socket.is_linked:
            links.new(socket.links[0].from_socket, emit.inputs["Color"])
        else:
            v = socket.default_value
            if isinstance(v, float):
                emit.inputs["Color"].default_value = (v, v, v, 1.0)
            else:
                emit.inputs["Color"].default_value = v
        original = out.inputs["Surface"].links[0].from_socket if out.inputs["Surface"].is_linked else None
        links.new(emit.outputs["Emission"], out.inputs["Surface"])
        swaps.append((mat, emit, original, out))
    _bake(ob, "EMIT", image, materials, source=source, cage_extrusion=cage_extrusion,
          max_ray_distance=max_ray_distance)
    for mat, emit, original, out in swaps:
        tree, nodes, links = _nodes(mat)
        if original is not None:
            links.new(original, out.inputs["Surface"])
        nodes.remove(emit)


def _pixels(image):
    import numpy as np
    buf = np.empty(image.size[0] * image.size[1] * 4, dtype=np.float32)
    image.pixels.foreach_get(buf)
    return buf.reshape(image.size[1], image.size[0], 4)


def _save(image, path):
    # 미리보기에서 연 파일도 교체할 수 있게 새 파일을 완성한 뒤 바꾼다.
    import uuid
    temporary = path + '.' + uuid.uuid4().hex + '.png'
    image.filepath_raw = temporary
    image.file_format = "PNG"
    try:
        image.save()
        os.replace(temporary, path)
    finally:
        image.filepath_raw = path
        if os.path.exists(temporary):
            os.remove(temporary)


def bake_textures(ob, asset_name, out_dir, size=1024, ao_samples=AO_SAMPLES):
    """BaseColor(_D, sRGB), Normal(_N, DirectX), ORM(_ORM, linear), 필요하면 _E.

    UE는 노멀의 G를 뒤집어 쓴다(ambientCG의 NormalDX를 _N으로 쓰는 것과
    같은 관례). Blender는 OpenGL로 굽으므로 저장 전에 G를 뒤집는다.
    """
    import numpy as np

    materials = _baked_materials(ob)
    if not materials:
        raise RuntimeError(f"{asset_name}: 굽을 재질이 없다")
    os.makedirs(out_dir, exist_ok=True)
    outputs = {}

    base = _new_image(f"{asset_name}_D", size, srgb=True)
    # 확산색 패스는 금속성 1인 면을 검게 굽는다. 재질 입력의 색을 그대로 저장한다.
    _bake_input_via_emit(ob, base, materials, "Base Color")
    outputs["D"] = os.path.join(out_dir, f"{asset_name}_D.png")
    _save(base, outputs["D"])

    normal = _new_image(f"{asset_name}_N", size, srgb=False)
    _bake(ob, "NORMAL", normal, materials, samples=1)
    px = _pixels(normal)
    px[:, :, 1] = 1.0 - px[:, :, 1]
    normal.pixels.foreach_set(px.reshape(-1))
    outputs["N"] = os.path.join(out_dir, f"{asset_name}_N.png")
    _save(normal, outputs["N"])

    rough = _new_image(f"{asset_name}_R", size, srgb=False)
    _bake(ob, "ROUGHNESS", rough, materials, samples=1)
    metal = _new_image(f"{asset_name}_M", size, srgb=False)
    _bake_input_via_emit(ob, metal, materials, "Metallic")
    ao = _new_image(f"{asset_name}_A", size, srgb=False)
    _bake(ob, "AO", ao, materials, samples=ao_samples)

    orm = _new_image(f"{asset_name}_ORM", size, srgb=False)
    packed = np.ones((size, size, 4), dtype=np.float32)
    packed[:, :, 0] = _pixels(ao)[:, :, 0]
    packed[:, :, 1] = _pixels(rough)[:, :, 0]
    packed[:, :, 2] = _pixels(metal)[:, :, 0]
    orm.pixels.foreach_set(packed.reshape(-1))
    outputs["ORM"] = os.path.join(out_dir, f"{asset_name}_ORM.png")
    _save(orm, outputs["ORM"])

    if _has_emissive(ob):
        emit = _new_image(f"{asset_name}_E", size, srgb=True)
        _bake(ob, "EMIT", emit, materials, samples=1)
        outputs["E"] = os.path.join(out_dir, f"{asset_name}_E.png")
        _save(emit, outputs["E"])

    for img in (base, normal, rough, metal, ao, orm):
        bpy.data.images.remove(img)
    return outputs


def decimate(ob, target_triangles):
    """Collapse 데시메이트로 삼각형 수를 맞춘다. 결과는 삼각형 메시."""
    current = triangle_count(ob)
    if current <= target_triangles:
        return ob
    mod = ob.modifiers.new("Decimate", "DECIMATE")
    mod.decimate_type = "COLLAPSE"
    mod.ratio = max(0.001, target_triangles / current)
    mod.use_collapse_triangulate = True
    apply_modifiers(ob)
    log(f"  decimate {current} -> {triangle_count(ob)} tris")
    return ob


def _source_materials(ob):
    return [m for m in ob.data.materials if m is not None]


def tint_box(ob, lo, hi, hue_deg=0.0, sat_mul=1.0, val_mul=1.0):
    """원본 재질의 Base Color에서 상자 안만 색상(도)·채도·명도를 바꾼다. 굽기가 이 색을 옮긴다.

    상자는 굽는 순간의 물체 좌표(m)로 준다. refine·rig 스크립트는 굽기 전에 Y를 뒤집으므로
    Y로 고르는 상자는 뒤집힌 뒤의 부호로 적는다. X와 Z만 쓰면 신경 쓸 일이 없다.
    같은 물건을 따로 뽑은 생성물의 색을 맞추거나, 시안에 없던 뒷면을 지어낸 색을 고칠 때 쓴다.
    """
    for mat in ob.data.materials:
        if mat is None or not mat.use_nodes:
            continue
        tree, nodes, links = _nodes(mat)
        bsdf = next((n for n in nodes if n.type == "BSDF_PRINCIPLED"), None)
        if bsdf is None or not bsdf.inputs["Base Color"].is_linked:
            continue
        source = bsdf.inputs["Base Color"].links[0].from_socket
        hsv = nodes.new("ShaderNodeHueSaturation")
        hsv.inputs["Hue"].default_value = 0.5 + hue_deg / 360.0
        hsv.inputs["Saturation"].default_value = sat_mul
        hsv.inputs["Value"].default_value = val_mul
        links.new(source, hsv.inputs["Color"])
        geometry = nodes.new("ShaderNodeNewGeometry")
        split = nodes.new("ShaderNodeSeparateXYZ")
        links.new(geometry.outputs["Position"], split.inputs["Vector"])
        inside = None
        for axis, low, high in zip("XYZ", lo, hi):
            for edge, operation in ((low, "GREATER_THAN"), (high, "LESS_THAN")):
                test = nodes.new("ShaderNodeMath")
                test.operation = operation
                test.inputs[1].default_value = edge
                links.new(split.outputs[axis], test.inputs[0])
                if inside is None:
                    inside = test
                    continue
                both = nodes.new("ShaderNodeMath")
                both.operation = "MULTIPLY"
                links.new(inside.outputs[0], both.inputs[0])
                links.new(test.outputs[0], both.inputs[1])
                inside = both
        links.new(inside.outputs[0], hsv.inputs["Fac"])
        links.new(hsv.outputs["Color"], bsdf.inputs["Base Color"])
    log(f"  tint box {[round(v, 3) for v in lo]}..{[round(v, 3) for v in hi]} "
        f"hue {hue_deg:+.1f}deg sat x{sat_mul} val x{val_mul}")


def prepare_organic_source(ob, roughness_floor=0.58):
    """살과 천의 원본에서 조각난 면 노멀과 잘못 추정된 금속 반사를 걷어 낸다.

    저밀도 몸만 매끈하게 만들어도 고밀도 원본의 삼각 면을 노멀로 다시 구우면
    종이 같은 주름이 돌아온다. 베이크 원본부터 연속된 표면으로 다뤄야 한다.
    """
    for poly in ob.data.polygons:
        poly.use_smooth = True
    if ob.data.has_custom_normals:
        set_active(ob)
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
    for mat in ob.data.materials:
        if mat is None:
            continue
        bsdf = _principled(mat)
        tree, nodes, links = _nodes(mat)
        metal = bsdf.inputs["Metallic"]
        for link in list(metal.links):
            links.remove(link)
        metal.default_value = 0.0
        rough = bsdf.inputs["Roughness"]
        if rough.is_linked:
            original = rough.links[0].from_socket
            floor = nodes.new("ShaderNodeMath")
            floor.operation = "MAXIMUM"
            floor.inputs[1].default_value = roughness_floor
            links.new(original, floor.inputs[0])
            links.new(floor.outputs[0], rough)
        else:
            rough.default_value = max(float(rough.default_value), roughness_floor)


def remove_small_islands(ob, max_diameter=0.035, keep_largest=False):
    """리메시 뒤 손끝·머리 둘레에 떠 있는 작은 조각만 없앤다(단위 m)."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    unseen = set(bm.verts)
    groups = []
    while unseen:
        seed = unseen.pop()
        group, pending = [seed], [seed]
        while pending:
            current = pending.pop()
            for edge in current.link_edges:
                other = edge.other_vert(current)
                if other in unseen:
                    unseen.remove(other)
                    pending.append(other)
                    group.append(other)
        groups.append(group)
    largest = max((len(group) for group in groups), default=0)
    removed = []
    for group in groups:
        if len(group) == largest:
            continue
        low = Vector(tuple(min(v.co[i] for v in group) for i in range(3)))
        high = Vector(tuple(max(v.co[i] for v in group) for i in range(3)))
        if keep_largest or (high - low).length < max_diameter:
            removed.extend(group)
    if removed:
        bmesh.ops.delete(bm, geom=removed, context="VERTS")
        bm.to_mesh(ob.data)
        ob.data.update()
    bm.free()
    log(f"  cleanup islands={len(groups)} removed_vertices={len(removed)}")
    return len(removed)


def bake_from_high(low, high, asset_name, out_dir, size=2048, ao_samples=AO_SAMPLES,
                   cage_extrusion=0.02, max_ray_distance=0.0):
    """고밀도(high)의 색·거칠기·금속성·노멀·AO를 저밀도(low)의 UV로 굽는다.

    low에는 굽기용 빈 재질 하나만 있으면 된다. high의 재질은 glTF 임포터가
    만든 것(정점색이나 텍스처를 Base Color에 꽂은 Principled)을 그대로 쓴다.
    결과 파일 이름과 채널 규약은 bake_textures와 같다.
    """
    import numpy as np

    os.makedirs(out_dir, exist_ok=True)
    if not low.data.materials or low.data.materials[0] is None:
        target_mat = bpy.data.materials.new(f"{asset_name}_Baked")
        _principled(target_mat)
        target_mat["ig_kind"] = "baked"
        assign_material(low, target_mat)
    low_materials = [m for m in low.data.materials if m is not None]
    high_materials = _source_materials(high)
    high.hide_render = False
    outputs = {}
    common = {"source": high, "cage_extrusion": cage_extrusion, "max_ray_distance": max_ray_distance}

    base = _new_image(f"{asset_name}_D", size, srgb=True)
    _bake_input_via_emit(low, base, low_materials, "Base Color",
                         source_materials=high_materials, **common)
    outputs["D"] = os.path.join(out_dir, f"{asset_name}_D.png")
    _save(base, outputs["D"])

    normal = _new_image(f"{asset_name}_N", size, srgb=False)
    _bake(low, "NORMAL", normal, low_materials, samples=1, **common)
    px = _pixels(normal)
    px[:, :, 1] = 1.0 - px[:, :, 1]
    normal.pixels.foreach_set(px.reshape(-1))
    outputs["N"] = os.path.join(out_dir, f"{asset_name}_N.png")
    _save(normal, outputs["N"])

    rough = _new_image(f"{asset_name}_R", size, srgb=False)
    _bake(low, "ROUGHNESS", rough, low_materials, samples=1, **common)
    metal = _new_image(f"{asset_name}_M", size, srgb=False)
    _bake_input_via_emit(low, metal, low_materials, "Metallic", source=high,
                         source_materials=high_materials, cage_extrusion=cage_extrusion,
                         max_ray_distance=max_ray_distance)
    # AO는 저밀도 자기 자신으로 굽는다. 고밀도에서 옮기면 광선이 케이지 안에서
    # 자기 표면에 걸려 온통 검게 나온다.
    high.hide_render = True
    ao = _new_image(f"{asset_name}_A", size, srgb=False)
    _bake(low, "AO", ao, low_materials, samples=ao_samples)
    high.hide_render = False

    orm = _new_image(f"{asset_name}_ORM", size, srgb=False)
    packed = np.ones((size, size, 4), dtype=np.float32)
    packed[:, :, 0] = _pixels(ao)[:, :, 0]
    packed[:, :, 1] = _pixels(rough)[:, :, 0]
    packed[:, :, 2] = _pixels(metal)[:, :, 0]
    orm.pixels.foreach_set(packed.reshape(-1))
    outputs["ORM"] = os.path.join(out_dir, f"{asset_name}_ORM.png")
    _save(orm, outputs["ORM"])
    for img in (base, normal, rough, metal, ao, orm):
        bpy.data.images.remove(img)
    return outputs


def bake_vertex_ao(ob, samples=32, attribute="Color"):
    """자기 폐색을 정점색(RGB)에 굽는다. 석고 재질의 골 분진(cavity_dust)이 읽는다."""
    me = ob.data
    if attribute not in me.color_attributes:
        me.color_attributes.new(name=attribute, type="BYTE_COLOR", domain="CORNER")
    me.color_attributes.active_color = me.color_attributes[attribute]
    scene = bpy.context.scene
    scene.render.bake.target = "VERTEX_COLORS"
    _bake(ob, "AO", None, [], samples=samples, target="VERTEX_COLORS")
    scene.render.bake.target = "IMAGE_TEXTURES"


def preview_material_from_bakes(ob, textures, name="PreviewBaked"):
    """구운 PNG로 Principled 재질을 만들어 미리보기 렌더가 실제 결과를 보여 주게 한다."""
    mat = bpy.data.materials.new(name)
    bsdf = _principled(mat)
    tree, nodes, links = _nodes(mat)
    base = nodes.new("ShaderNodeTexImage")
    base.image = bpy.data.images.load(textures["D"])
    base.image.colorspace_settings.name = "sRGB"
    links.new(base.outputs["Color"], bsdf.inputs["Base Color"])
    orm = nodes.new("ShaderNodeTexImage")
    orm.image = bpy.data.images.load(textures["ORM"])
    orm.image.colorspace_settings.name = "Non-Color"
    sep = nodes.new("ShaderNodeSeparateColor")
    links.new(orm.outputs["Color"], sep.inputs["Color"])
    links.new(sep.outputs["Green"], bsdf.inputs["Roughness"])
    links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
    nrm = nodes.new("ShaderNodeTexImage")
    nrm.image = bpy.data.images.load(textures["N"])
    nrm.image.colorspace_settings.name = "Non-Color"
    # 저장본은 DirectX(G 뒤집힘)라 Blender에서 보려면 도로 뒤집는다.
    sep_n = nodes.new("ShaderNodeSeparateColor")
    links.new(nrm.outputs["Color"], sep_n.inputs["Color"])
    inv = nodes.new("ShaderNodeMath")
    inv.operation = "SUBTRACT"
    inv.inputs[0].default_value = 1.0
    links.new(sep_n.outputs["Green"], inv.inputs[1])
    comb = nodes.new("ShaderNodeCombineColor")
    links.new(sep_n.outputs["Red"], comb.inputs["Red"])
    links.new(inv.outputs[0], comb.inputs["Green"])
    links.new(sep_n.outputs["Blue"], comb.inputs["Blue"])
    nmap = nodes.new("ShaderNodeNormalMap")
    links.new(comb.outputs["Color"], nmap.inputs["Color"])
    links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    if "E" in textures:
        emi = nodes.new("ShaderNodeTexImage")
        emi.image = bpy.data.images.load(textures["E"])
        links.new(emi.outputs["Color"], bsdf.inputs["Emission Color"])
        bsdf.inputs["Emission Strength"].default_value = 3.0
    mat["ig_kind"] = "preview"
    ob.data.materials.clear()
    ob.data.materials.append(mat)
    return mat


# --------------------------------------------------------------------------
# 충돌 · 내보내기 · 미리보기 · manifest
# --------------------------------------------------------------------------

def collision_hull(name, source_objects, index):
    """source_objects의 정점을 감싸는 볼록 껍데기 하나. UCX_<name>_NN."""
    bm = bmesh.new()
    for ob in source_objects:
        part = _world_bm(ob) if ob.type == "MESH" else None
        if part is None:
            continue
        temp = bpy.data.meshes.new("__hull_tmp")
        part.to_mesh(temp)
        part.free()
        bm.from_mesh(temp)
        bpy.data.meshes.remove(temp)
    result = bmesh.ops.convex_hull(bm, input=bm.verts)
    interior = [g for g in result["geom_interior"]] + [g for g in result["geom_unused"]]
    bmesh.ops.delete(bm, geom=interior, context="VERTS")
    hull_name = f"UCX_{name}_{index:02d}"
    ob = _link(bpy.data.objects.new(hull_name, _mesh_from_bm(hull_name, bm)))
    ob.display_type = "WIRE"
    # 껍데기가 렌더에 남으면 AO 광선이 전부 여기 막혀 문짝 AO가 0으로 구워진다.
    ob.hide_render = True
    return ob


def collision_box(name, size, location, index, rotation=(0.0, 0.0, 0.0)):
    """상자 충돌. UCX로 내보내되 정점 8개라 사실상 상자다."""
    hull_name = f"UCX_{name}_{index:02d}"
    ob = box(hull_name, size, location=location, rotation=rotation)
    ob.display_type = "WIRE"
    ob.hide_render = True
    return ob


def ensure_primary_uv(ob, name="UVMap"):
    """구운 텍스처의 좌표를 FBX의 첫 UV 채널로 내보낸다.

    Blender의 active_render는 FBX 채널 순서를 바꾸지 않는다. 이미지 원화용
    ImageUV가 먼저 생긴 메시도 UE의 TextureCoordinate(0)와 맞아야 한다.
    """
    layers = ob.data.uv_layers
    if name not in layers or layers[0].name == name:
        return False
    snapshots = []
    for layer in layers:
        values = [0.0] * (len(layer.data) * 2)
        layer.data.foreach_get("uv", values)
        snapshots.append((layer.name, values))
    snapshots.sort(key=lambda item: item[0] != name)
    while layers:
        layers.remove(layers[0])
    for layer_name, values in snapshots:
        layer = layers.new(name=layer_name)
        layer.data.foreach_set("uv", values)
    layers.active = layers[name]
    layers[name].active_render = True
    return True


def export_fbx(path, objects):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.context.view_layer.update()
    for other in list(bpy.context.view_layer.objects):
        if other is not None:
            other.select_set(False)
    for ob in objects:
        ob.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        object_types={"MESH"},
        use_mesh_modifiers=True,
        mesh_smooth_type="EDGE",
        use_tspace=True,
        add_leaf_bones=False,
        bake_anim=False,
        path_mode="STRIP",
        embed_textures=False,
        global_scale=1.0,
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_NONE",
        use_custom_props=False,
        **EXPORT_AXIS)
    log(f"  fbx -> {path} ({os.path.getsize(path)} bytes)")


def render_preview(ob, path, flashlight=False, size=(1280, 960), extra_objects=(),
                   camera_yaw_deg=30.0, camera_pitch_deg=10.0, distance_scale=1.45):
    """유리가 있으면 Cycles로 내부까지 확인한다. 손전등은 카메라 옆 스팟을 쓴다."""
    scene = bpy.context.scene
    has_glass = any(mat and mat.get("ig_kind") == "glass" for mat in ob.data.materials)
    scene.render.engine = "CYCLES" if has_glass else "BLENDER_EEVEE"
    if has_glass:
        scene.cycles.device = "CPU"
        scene.cycles.samples = 32
        scene.cycles.use_denoising = True
        scene.cycles.transmission_bounces = 8
    scene.render.resolution_x, scene.render.resolution_y = size
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.eevee.taa_render_samples = 32
    scene.view_settings.view_transform = "Filmic" if "Filmic" in [i.identifier for i in bpy.types.ColorManagedViewSettings.bl_rna.properties["view_transform"].enum_items] else scene.view_settings.view_transform
    scene.view_settings.exposure = 1.2 if flashlight else 0.0
    lo, hi = bounds(ob)
    for extra in extra_objects:
        elo, ehi = bounds(extra)
        lo = Vector((min(lo.x, elo.x), min(lo.y, elo.y), min(lo.z, elo.z)))
        hi = Vector((max(hi.x, ehi.x), max(hi.y, ehi.y), max(hi.z, ehi.z)))
    center = (lo + hi) * 0.5
    radius = max((hi - lo).length * 0.5, 0.05)
    yaw = math.radians(camera_yaw_deg)
    pitch = math.radians(camera_pitch_deg)
    dist = radius * distance_scale / math.tan(math.radians(20.0))
    cam_pos = center + Vector((
        -math.sin(yaw) * math.cos(pitch) * dist,
        -math.cos(yaw) * math.cos(pitch) * dist,
        math.sin(pitch) * dist))
    cam_data = bpy.data.cameras.new("__preview_cam")
    cam_data.angle = math.radians(40.0)
    cam = _link(bpy.data.objects.new("__preview_cam", cam_data))
    cam.location = cam_pos
    direction = center - cam_pos
    cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam

    world = bpy.data.worlds.new("__preview_world")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.04, 0.045, 0.05, 1.0) if flashlight else (0.35, 0.36, 0.38, 1.0)
    bg.inputs[1].default_value = 0.15 if flashlight else 1.0
    scene.world = world

    ground = box("__preview_ground", (radius * 10, radius * 10, 0.02),
                 location=(center.x, center.y, lo.z - 0.01))
    gmat = bpy.data.materials.new("__preview_ground_mat")
    gb = _principled(gmat)
    gb.inputs["Base Color"].default_value = (0.42, 0.40, 0.37, 1.0)
    gb.inputs["Roughness"].default_value = 0.85
    assign_material(ground, gmat)

    lights = []
    if flashlight:
        ld = bpy.data.lights.new("__spot", "SPOT")
        ld.energy = 2500.0 * max(1.0, radius)
        ld.spot_size = math.radians(34.0)
        ld.spot_blend = 0.55
        ld.color = (1.0, 0.96, 0.88)
        ld.shadow_soft_size = 0.02
        spot = _link(bpy.data.objects.new("__spot", ld))
        spot.location = cam_pos + Vector((0.0, 0.0, -0.15))
        spot.rotation_euler = cam.rotation_euler
        lights.append(spot)
    else:
        # 등은 대상에서 반지름의 두 배(최소 1 m) 떨어진다. 출력은 거리의
        # 제곱에 비례시켜야 큰 문짝과 작은 소화기가 같은 밝기로 찍힌다.
        light_distance = max(radius * 2.0, 1.0)
        for i, (offset, energy) in enumerate((
                (Vector((-1.0, -1.2, 1.4)), 190.0),
                (Vector((1.4, -0.6, 0.9)), 70.0),
                (Vector((0.3, 1.3, 1.2)), 50.0))):
            ld = bpy.data.lights.new(f"__key{i}", "AREA")
            ld.energy = energy * light_distance * light_distance
            ld.size = max(radius * 1.5, 0.4)
            light = _link(bpy.data.objects.new(f"__key{i}", ld))
            light.location = center + offset * light_distance
            light.rotation_euler = (center - light.location).to_track_quat("-Z", "Y").to_euler()
            lights.append(light)

    scene.render.filepath = path
    scene.render.image_settings.file_format = "PNG"
    bpy.ops.render.render(write_still=True)
    log(f"  preview -> {path}")

    for o in lights + [cam, ground]:
        data = o.data
        bpy.data.objects.remove(o, do_unlink=True)
        if isinstance(data, bpy.types.Mesh):
            bpy.data.meshes.remove(data)
        elif isinstance(data, bpy.types.Light):
            bpy.data.lights.remove(data)
        elif isinstance(data, bpy.types.Camera):
            bpy.data.cameras.remove(data)
    bpy.data.worlds.remove(world)
    bpy.data.materials.remove(gmat)


def emissive_strength(materials):
    """굽기 이미지는 8비트라 1.0에서 잘린다. 재질의 발광 세기 최댓값을 따로 남겨
    UE 인스턴스의 EmissiveStrength로 되살린다."""
    best = 0.0
    for mat in materials:
        if mat is None or not mat.get("ig_emissive"):
            continue
        bsdf = mat.node_tree.nodes.get("Principled BSDF") if mat.use_nodes else None
        if bsdf is not None:
            best = max(best, float(bsdf.inputs["Emission Strength"].default_value))
    return best


def write_manifest(out_dir, asset_name, mesh_class, fbx_path, textures, ob, slots,
                   notes="", origin="bottom-center", ue_bounds=None, emissive=0.0):
    lo, hi = ue_bounds if ue_bounds is not None else bounds(ob)
    manifest = {
        "name": asset_name,
        "mesh_class": mesh_class,
        "fbx": os.path.basename(fbx_path),
        "textures": {k: os.path.basename(v) for k, v in textures.items()},
        "slots": slots,
        "triangles": triangle_count(ob),
        "bounds_m": {"min": [round(v, 4) for v in lo], "max": [round(v, 4) for v in hi]},
        "origin": origin,
        "emissive_strength": round(emissive, 3),
        "blender": bpy.app.version_string,
        "notes": notes,
    }
    path = os.path.join(out_dir, "manifest.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    log(f"  manifest -> {path} tris={manifest['triangles']}")
    return manifest


def slot_layout(ob):
    """FBX 재질 슬롯 목록. baked 재질은 전부 'Baked' 한 슬롯으로 합치고
    유리는 'Glass'로 남긴다. 결합 뒤에 부른다."""
    me = ob.data
    # 빌더가 만든 'Glass' 원본이 남아 있으면 새 슬롯이 'Glass.001'이 되고,
    # UE 쪽 슬롯 이름 매칭이 빗나간다. 원본을 먼저 비켜 둔다.
    for taken in ("Baked", "Glass"):
        if taken in bpy.data.materials:
            bpy.data.materials[taken].name = taken + "_src"
    baked = bpy.data.materials.new("Baked")
    baked["ig_kind"] = "baked_merged"
    glass = None
    new_materials = [baked]
    remap = {}
    for index, mat in enumerate(me.materials):
        if mat is not None and mat.get("ig_kind") == "glass":
            if glass is None:
                glass = bpy.data.materials.new("Glass")
                new_materials.append(glass)
            remap[index] = new_materials.index(glass)
        else:
            remap[index] = 0
    return remap, new_materials


def finalize_slots(ob):
    """굽기가 끝난 뒤 재질을 Baked/Glass 두 슬롯으로 정리한다."""
    remap, new_materials = slot_layout(ob)
    me = ob.data
    # materials.clear()가 면의 material_index를 0으로 되돌린다. 슬롯을 갈아
    # 끼운 뒤에 다시 써야 유리 면이 Glass 슬롯에 남는다.
    indices = [remap.get(poly.material_index, 0) for poly in me.polygons]
    me.materials.clear()
    for mat in new_materials:
        me.materials.append(mat)
    for poly, index in zip(me.polygons, indices):
        poly.material_index = index
    me.update()
    return [m.name for m in new_materials]


def build_asset(asset_name, mesh_class, parts, out_root, collision_parts=None,
                notes="", texture_size=None, preview=True, sharp_angle=30.0,
                uv_margin=0.004, extra_export=(), preview_yaw=30.0, raw_uv=False,
                mirror_print_for_ue=False, origin="bottom-center", mirror_print_uv=False):
    """빌더의 마지막 공통 단계.

    parts: 결합할 오브젝트 목록(재질 붙어 있어야 함)
    collision_parts: [[obj, ...], ...] 그룹마다 볼록 껍데기 하나. None이면 전체 하나.
    """
    if mirror_print_uv and (mirror_print_for_ue or raw_uv):
        raise ValueError("인쇄 UV 보정과 전체 반전/raw_uv는 함께 쓸 수 없다")
    out_dir = os.path.join(out_root, asset_name)
    os.makedirs(out_dir, exist_ok=True)
    started = time.time()

    # 충돌은 결합 전 원본 부품 기준으로 만든다. 결합하면 부품 경계가 사라진다.
    hulls = []
    groups = collision_parts if collision_parts is not None else [list(parts)]
    for index, group in enumerate(groups, start=1):
        # 그룹 원본이 결합에 들어가 사라지기 전에 사본으로 껍데기를 뜬다.
        copies = []
        for src in group:
            dup = src.copy()
            dup.data = src.data.copy()
            _link(dup)
            copies.append(dup)
        hulls.append(collision_hull(asset_name, copies, index))
        for dup in copies:
            data = dup.data
            bpy.data.objects.remove(dup, do_unlink=True)
            bpy.data.meshes.remove(data)

    ob = join(list(parts), asset_name)
    mark_sharp_by_angle(ob, sharp_angle)
    tris = triangle_count(ob)
    log(f"{asset_name}: joined tris={tris}")
    ue_bounds = bounds(ob)
    emissive = emissive_strength([m for m in ob.data.materials])

    # 미리보기는 저작 좌표(UE 좌표)에서 찍는다. 카메라가 -Y 쪽, 즉 정면에 선다.
    if preview:
        # preview_yaw: 카메라가 서는 방위. 0이 -Y(기본 앞면), 90이 -X, 180이 +Y.
        render_preview(ob, os.path.join(out_dir, f"{asset_name}_preview.png"), camera_yaw_deg=preview_yaw)
        render_preview(ob, os.path.join(out_dir, f"{asset_name}_preview_torch.png"), flashlight=True,
                       camera_yaw_deg=preview_yaw)

    # 인쇄만 보정한다. 모델 전체를 뒤집으면 책등·손잡이·개봉 탭까지 옮겨진다.
    # ImageUV를 쓰는 인쇄 재질만 바뀌며, 베이크 UV와 물체 좌표는 보존한다.
    if mirror_print_uv:
        print_uv = ob.data.uv_layers.get("ImageUV")
        if print_uv is None:
            raise ValueError(f"{asset_name}: 인쇄용 ImageUV가 없다")
        for loop in print_uv.data:
            loop.uv.x = 1.0 - loop.uv.x

    # 여기서부터는 FBX 좌표. UE가 다시 뒤집어 저작 좌표로 돌려놓는다.
    mirror_y(ob)
    for hull in hulls:
        mirror_y(hull)
    if raw_uv:
        # 라벨을 감는 슬리브처럼 UE 쪽 재질이 UV0을 직접 읽는 메시. 스마트 UV도
        # 굽기도 하지 않고 빌더가 편 UV 그대로 내보낸다. 재질은 씬이 준다.
        textures = {}
        slots = [slot.material.name if slot.material else "Baked" for slot in ob.material_slots] or ["Baked"]
    else:
        uv_smart(ob, margin=uv_margin)
        size = texture_size or TEXTURE_SIZE[mesh_class]
        textures = bake_textures(ob, asset_name, out_dir, size=size)
        slots = finalize_slots(ob)
        ensure_primary_uv(ob)
    if mirror_print_for_ue:
        # UE에서 -Y 면을 바라볼 때의 화면 오른쪽은 Blender와 반대다.
        # 구운 UV는 보존하고 좌우 정점·면 방향을 함께 바꿔 인쇄를 읽게 한다.
        for item in [ob] + hulls:
            for vertex in item.data.vertices:
                vertex.co.x = -vertex.co.x
            bm = bmesh.new()
            bm.from_mesh(item.data)
            bmesh.ops.reverse_faces(bm, faces=bm.faces)
            bm.to_mesh(item.data)
            bm.free()
            item.data.update()
    fbx_path = os.path.join(out_dir, f"{asset_name}.fbx")
    export_fbx(fbx_path, [ob] + hulls + list(extra_export))
    manifest = write_manifest(out_dir, asset_name, mesh_class, fbx_path, textures, ob, slots, notes,
                              origin=origin, ue_bounds=ue_bounds, emissive=emissive)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, f"{asset_name}.blend"))
    log(f"{asset_name}: done in {time.time() - started:.1f}s")
    return manifest


def out_root_from_argv(default_relative="Content/SourceArt/Blender"):
    """`-- <out_dir>` 인자. 없으면 저장소의 Content/SourceArt/Blender."""
    if "--" in sys.argv:
        args = sys.argv[sys.argv.index("--") + 1:]
        if args:
            return os.path.abspath(args[0])
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(here, "..", "..", default_relative))
