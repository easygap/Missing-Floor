"""생성된 GLB(TRELLIS.2·Pixal3D)를 게임 메시로 다듬는다.

generate_3d_comfy.py가 남긴 원본은 삼각형 20만 개에 실제 치수도 방향도 없는
덩어리다. 여기서 하는 일:

1. GLB를 읽어 한 메시로 합치고, 요청한 yaw로 돌려 정면을 -Y에 둔다
2. 한 축을 실제 치수(cm)에 맞춰 균일 배율을 걸고 원점을 바닥 중심으로
3. 고밀도 사본을 남기고 저밀도를 예산(hero 12000 등)까지 데시메이트, 스마트 UV
4. 고밀도 → 저밀도로 BaseColor·Normal(DX)·Roughness·Metallic을 굽고 AO는
   저밀도 자기 자신으로 굽는다. --vertex-ao 면 정점색에도 AO를 남긴다
5. 미리보기 두 장(스튜디오·손전등), Y 반전, FBX·manifest

    blender -b --factory-startup --python Scripts/blender/refine_generated.py -- \\
        --glb <raw/pbr_00001_.glb> --name SM_ListenerEntityCrawl --mesh-class hero \\
        --length 210 --yaw 0 --vertex-ao --out Content/SourceArt/Blender

--length/--width/--height 중 하나만 준다. 그 축이 그 cm가 되도록 전체를 균일하게
키운다(길이 X, 폭 Y, 높이 Z).
"""

import argparse
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import ig_blender_lib as ig  # noqa: E402

BUDGET = {"hero": 12000, "prop": 3000, "large": 9000}


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--glb", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--mesh-class", choices=list(BUDGET), default="hero")
    parser.add_argument("--budget", type=int, default=0, help="LOD0 삼각형. 0이면 mesh-class 기본")
    parser.add_argument("--length", type=float, default=0.0, help="X 크기 cm")
    parser.add_argument("--width", type=float, default=0.0, help="Y 크기 cm")
    parser.add_argument("--height", type=float, default=0.0, help="Z 크기 cm")
    parser.add_argument("--yaw", type=float, default=0.0, help="Z축 회전(도). 정면을 -Y로 맞춘다")
    parser.add_argument("--origin", choices=["bottom", "center"], default="bottom")
    parser.add_argument("--texture-size", type=int, default=0)
    parser.add_argument("--vertex-ao", action="store_true")
    parser.add_argument("--organic", action="store_true", help="사람·생물의 면 노멀과 비금속 반사를 정리한다")
    parser.add_argument("--front-photo", help="정면 원본으로 눈과 피부의 색을 보완한다")
    parser.add_argument("--smooth-iterations", type=int, default=0, help="데시메이트 전 스무딩 횟수")
    parser.add_argument("--voxel-remesh", type=float, default=0.0,
                        help="데시메이트 전 복셀 리메시 크기(m). 털·얇은 조각으로 깨진 표면을 한 덩어리로 녹인다")
    parser.add_argument("--out", default=None)
    parser.add_argument("--notes", default="")
    parser.add_argument("--clip-y-min", type=float, default=None, help="중심 맞춘 뒤 이 Y(m) 앞쪽을 잘라낸다")
    parser.add_argument("--clip-y-max", type=float, default=None, help="중심 맞춘 뒤 이 Y(m) 뒤쪽을 잘라낸다")
    parser.add_argument("--shift", default="0,0,0", help="자른 뒤 원점 이동(m) x,y,z")
    parser.add_argument("--squash", default="1,1,1",
                        help="균일 배율 뒤 축별 배율 x,y,z. 생성물이 기준보다 늘어진 축을 누를 때")
    parser.add_argument("--rot-x", type=float, default=0.0, help="X축 회전(도). yaw보다 먼저 건다. 누운 생성물을 세울 때")
    parser.add_argument("--rot-y", type=float, default=0.0, help="Y축 회전(도). yaw보다 먼저 건다")
    parser.add_argument("--recolor-above", default="",
                        help="z(m),색상(도),채도 배율,명도 배율. 다듬은 좌표에서 이 높이 위의 색만 굽기 전에 바꾼다. "
                             "같은 물건을 따로 뽑은 두 생성물의 색을 맞출 때(세워 둔 오토바이 배달통)")
    parser.add_argument("--views", action="store_true",
                        help="회전·배율 전 원본을 정면(-Y)·측면(-X)·위에서 세 장 찍고 끝낸다")
    parser.add_argument("--probe", action="store_true",
                        help="주축 방향·양끝 높이만 찍고 끝낸다. yaw를 정할 때 쓴다")
    return parser.parse_args(argv)


def probe(high):
    """긴 축이 어느 방향인지, 어느 끝이 높은지 찍는다. --yaw 값을 고르는 근거."""
    import numpy as np
    pts = np.array([[v.co.x, v.co.y, v.co.z] for v in high.data.vertices])
    centered = pts - pts.mean(axis=0)
    w, vecs = np.linalg.eigh(np.cov(centered.T))
    axis = vecs[:, int(np.argmax(w))]
    if axis[0] < 0:
        axis = -axis
    yaw_now = math.degrees(math.atan2(axis[1], axis[0]))
    along = centered @ axis
    head_end = pts[along > np.percentile(along, 90)]
    tail_end = pts[along < np.percentile(along, 10)]
    lo, hi = ig.bounds(high)
    ig.log(f"PROBE size(m)={[round(v, 3) for v in (hi - lo)]} principal_xy_yaw={yaw_now:.1f}deg "
           f"(+end mean z={head_end[:, 2].mean():.3f}, -end mean z={tail_end[:, 2].mean():.3f})")
    ig.log(f"PROBE 긴 축을 +X로 돌리려면 --yaw {-yaw_now:.1f} (또는 {180 - yaw_now:.1f} 로 끝을 바꿈)")


def recolor_above(ob, spec):
    """이 높이 위만 굽기 전에 색을 바꾼다. Z만 보므로 굽기 전 Y 반전과 상관없다."""
    z, hue_deg, sat_mul, val_mul = (float(v) for v in spec.split(","))
    ig.tint_box(ob, (-1.0e4, -1.0e4, z), (1.0e4, 1.0e4, 1.0e4), hue_deg, sat_mul, val_mul)


def clip_plane(ob, axis, value, keep_below):
    """축 하나의 평면으로 메시를 자르고 잘린 구멍을 메운다."""
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    normal = [0.0, 0.0, 0.0]
    normal[axis] = 1.0 if keep_below else -1.0
    co = [0.0, 0.0, 0.0]
    co[axis] = value
    result = bmesh.ops.bisect_plane(
        bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], dist=1e-5,
        plane_co=co, plane_no=normal, clear_outer=True, clear_inner=False)
    cut_edges = [e for e in result["geom_cut"] if isinstance(e, bmesh.types.BMEdge)]
    if cut_edges:
        try:
            bmesh.ops.holes_fill(bm, edges=cut_edges, sides=0)
        except Exception:  # noqa: BLE001 - 열린 고리가 아니면 그냥 둔다
            pass
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.update()
    ig.log(f"  clip axis={axis} at {value:.3f} keep_below={keep_below} -> {ig.triangle_count(ob)} tris")


def import_glb(path):
    bpy.ops.import_scene.gltf(filepath=path)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise RuntimeError("GLB에 메시가 없다")
    for o in list(bpy.context.scene.objects):
        if o.type != "MESH" and not any(m.parent == o for m in meshes):
            pass
    # 빈 오브젝트 부모 변환까지 정점에 굽고 하나로 합친다.
    joined = ig.join(meshes, "generated_high")
    for o in list(bpy.context.scene.objects):
        if o.type != "MESH":
            bpy.data.objects.remove(o, do_unlink=True)
    # glTF는 UV 이음매마다 정점을 갈라 둔다. 배달 오토바이 원본은 조각이 4969개였고,
    # 붙이지 않고 줄이면 조각마다 따로 줄어서 틈과 가시가 옷 전체에 생긴다.
    # UV는 면 모서리마다 따로 있으니 정점을 붙여도 원본 텍스처 좌표는 그대로다.
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(joined.data)
    before = len(bm.verts)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=0.00002)
    bm.to_mesh(joined.data)
    bm.free()
    joined.data.update()
    ig.log(f"  weld seams {before} -> {len(joined.data.vertices)} verts")
    # glTF 임포터의 Color Attribute 노드는 layer_name이 비어 있어 활성 속성을
    # 읽는다. 결합 뒤에도 정점색을 읽도록 이름을 박아 둔다.
    color_names = [a.name for a in joined.data.color_attributes]
    if color_names:
        for mat in joined.data.materials:
            if mat is None or not mat.use_nodes:
                continue
            for node in mat.node_tree.nodes:
                if node.type == "VERTEX_COLOR" and node.layer_name not in color_names:
                    node.layer_name = color_names[0]
    return joined


def main():
    args = parse_args()
    out_root = os.path.abspath(args.out) if args.out else os.path.abspath(
        os.path.join(HERE, "..", "..", "Content", "SourceArt", "Blender"))
    name = args.name
    out_dir = os.path.join(out_root, name)
    os.makedirs(out_dir, exist_ok=True)

    ig.reset_scene()
    high = import_glb(os.path.abspath(args.glb))
    ig.log(f"{name}: imported {ig.triangle_count(high)} tris, materials={[m.name for m in high.data.materials if m]}")
    if args.probe:
        probe(high)
        return
    if args.views:
        for tag, yaw, pitch in (("front", 0.0, 4.0), ("side", 90.0, 4.0), ("top", 0.0, 88.0)):
            ig.render_preview(high, os.path.join(out_dir, f"{name}_view_{tag}.png"),
                              camera_yaw_deg=yaw, camera_pitch_deg=pitch, distance_scale=1.6)
        return

    # 누운 생성물 세우기: X·Y축 회전을 yaw보다 먼저.
    if args.rot_x or args.rot_y:
        high.rotation_euler = (math.radians(args.rot_x), math.radians(args.rot_y), 0.0)
        bpy.context.view_layer.update()
        bm_ob = ig._world_bm(high)
        me = bpy.data.meshes.new(high.data.name)
        bm_ob.to_mesh(me)
        bm_ob.free()
        for mat in high.data.materials:
            me.materials.append(mat)
        old = high.data
        high.data = me
        high.rotation_euler = (0.0, 0.0, 0.0)
        bpy.data.meshes.remove(old)
        if me.color_attributes:
            me.color_attributes.active_color = me.color_attributes[0]

    # 회전: 정면을 -Y로.
    if args.yaw:
        high.rotation_euler = (0.0, 0.0, math.radians(args.yaw))
        ig.apply_modifiers(high)
        bpy.context.view_layer.update()
        bm_ob = ig._world_bm(high)
        me = bpy.data.meshes.new(high.data.name)
        bm_ob.to_mesh(me)
        bm_ob.free()
        for mat in high.data.materials:
            me.materials.append(mat)
        old = high.data
        high.data = me
        high.rotation_euler = (0.0, 0.0, 0.0)
        bpy.data.meshes.remove(old)

    lo, hi = ig.bounds(high)
    size = hi - lo
    targets = [(args.length, 0), (args.width, 1), (args.height, 2)]
    scale = 1.0
    for cm, axis in targets:
        if cm > 0.0 and size[axis] > 1e-6:
            scale = (cm / 100.0) / size[axis]
            break
    squash = Vector([float(v) for v in args.squash.split(",")])
    for v in high.data.vertices:
        v.co *= scale
        v.co = Vector((v.co.x * squash.x, v.co.y * squash.y, v.co.z * squash.z))
    lo, hi = ig.bounds(high)
    center = (lo + hi) * 0.5
    shift = Vector((center.x, center.y, lo.z if args.origin == "bottom" else center.z))
    for v in high.data.vertices:
        v.co -= shift
    high.data.update()
    if args.organic:
        ig.prepare_organic_source(high)
    if args.recolor_above:
        recolor_above(high, args.recolor_above)
    if args.front_photo:
        from project_character_face import apply
        apply(high, os.path.abspath(args.front_photo))
    # 생성 메시에 딸려 온 배경(벽감·받침)을 평면으로 잘라내고 구멍을 메운다.
    for axis, value, keep_below in ((1, args.clip_y_max, True), (1, args.clip_y_min, False)):
        if value is None:
            continue
        clip_plane(high, axis, value, keep_below)
    shift = Vector([float(v) for v in args.shift.split(",")])
    if shift.length > 0.0:
        for v in high.data.vertices:
            v.co += shift
        high.data.update()
    lo, hi = ig.bounds(high)
    ig.log(f"{name}: scale x{scale:.4f} -> size {(hi - lo).x * 100:.1f} x {(hi - lo).y * 100:.1f} x {(hi - lo).z * 100:.1f} cm"
           f" bounds {[round(v * 100, 1) for v in lo]}..{[round(v * 100, 1) for v in hi]}")

    # 저밀도 사본.
    low = high.copy()
    low.data = high.data.copy()
    low.name = name
    low.data.name = name
    ig._link(low)
    if args.voxel_remesh > 0.0:
        # TRELLIS는 털을 얇은 조각 껍질로 만든다. 복셀 리메시가 그 조각을
        # 닫힌 한 표면으로 녹이고, 색은 여전히 고밀도 원본에서 굽는다.
        mod = low.modifiers.new("Remesh", "REMESH")
        mod.mode = "VOXEL"
        mod.voxel_size = args.voxel_remesh
        mod.use_smooth_shade = True
        ig.apply_modifiers(low)
        ig.log(f"{name}: voxel remesh {args.voxel_remesh} m -> {ig.triangle_count(low)} tris")
    if args.smooth_iterations > 0:
        mod = low.modifiers.new("Smooth", "SMOOTH")
        mod.iterations = args.smooth_iterations
        mod.factor = 0.5
        ig.apply_modifiers(low)
    budget = args.budget or BUDGET[args.mesh_class]
    if args.organic:
        ig.remove_small_islands(low)
    ig.decimate(low, budget)
    for poly in low.data.polygons:
        poly.use_smooth = True
    low.data.materials.clear()
    ue_bounds = ig.bounds(low)

    # 미리보기는 저작 좌표에서. 굽기 전이라 고밀도(정점색/텍스처)를 보여 준다.
    ig.render_preview(high, os.path.join(out_dir, f"{name}_source_preview.png"))

    ig.mirror_y(low)
    ig.mirror_y(high)
    ig.uv_smart(low, margin=0.003)
    texture_size = args.texture_size or ig.TEXTURE_SIZE[args.mesh_class]
    extrusion = max((hi - lo).length * 0.01, 0.005)
    textures = ig.bake_from_high(low, high, name, out_dir, size=texture_size,
                                 cage_extrusion=extrusion, max_ray_distance=extrusion * 4.0)
    if args.vertex_ao:
        high.hide_render = True
        ig.bake_vertex_ao(low)
        high.hide_render = False

    # 고밀도는 이제 필요 없다. 미리보기와 내보내기에서 빠져야 한다.
    high_mesh = high.data
    bpy.data.objects.remove(high, do_unlink=True)
    bpy.data.meshes.remove(high_mesh)

    # FBX용 Y 반전 상태에서는 카메라도 반대쪽에 놓아 정면을 확인한다.
    ig.preview_material_from_bakes(low, textures)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_preview.png"), camera_yaw_deg=180)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_preview_torch.png"), flashlight=True, camera_yaw_deg=180)

    slots = ig.finalize_slots(low)
    ig.ensure_primary_uv(low)
    fbx_path = os.path.join(out_dir, f"{name}.fbx")
    ig.export_fbx(fbx_path, [low])
    manifest = ig.write_manifest(out_dir, name, args.mesh_class, fbx_path, textures, low, slots,
                                 notes=args.notes or f"생성 GLB {os.path.basename(args.glb)}에서 다듬음",
                                 origin=args.origin, ue_bounds=ue_bounds)
    manifest_path = os.path.join(out_dir, "manifest.json")
    import json
    with open(manifest_path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    data["generated_from"] = os.path.relpath(os.path.abspath(args.glb), out_root).replace("\\", "/")
    data["vertex_ao"] = bool(args.vertex_ao)
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, f"{name}.blend"))
    ig.log(f"{name}: done tris={ig.triangle_count(low)}")


main()
