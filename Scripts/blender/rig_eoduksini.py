"""생성 GLB를 어둑시니의 스켈레탈 메시로 다듬는다.

어둑시니는 걷지 않는다. 바라보는 동안 자라고, 다 자라면 허리를 숙여 보는 사람 위로
덮어 온다(AIGShadowFigure). 원기둥과 구로 짓던 몸을 이걸로 바꾸면서 허리가 이음매 없이
휘도록 뼈 넷(root·spine·arm_l·arm_r)을 단다. 동작은 굽지 않는다. 게임이 뼈를 직접 돌린다
(UPoseableMeshComponent).

1. GLB를 읽고 yaw로 정면을 +X에 둔다. 키를 맞추고 원점은 발밑 가운데.
2. 원본 재질을 젖은 검정으로 바꾼다. 점토색에 남은 주름 음영은 비율로 살리면서
   M_PlasticDark 밝기로 낮추고, 거칠기를 0.3대로 내린다.
3. 저밀도 사본(복셀 리메시 → 데시메이트)에 색·노멀·ORM을 굽는다.
4. 단면으로 팔을 찾는다. 팔은 겨드랑이 아래로 몸통과 틈을 두고 늘어져 있다.
5. 웨이트: 허리 위아래 띠에서 root와 spine을 섞는다. 팔은 팔뼈에 붙이고 어깨에서만
   spine과 섞는다. 숙일 때 게임이 팔을 거꾸로 돌려 늘어진 채로 두기 때문이다.

    blender -b --factory-startup --python Scripts/blender/rig_eoduksini.py -- \\
        --glb Content/SourceArt/Generated/Eoduksini/trellis1024-s56/raw/pbr_00001_.glb
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
from rig_crawler import bake_rotation, debug_bone_props, import_glb, remove_props, vertex_array  # noqa: E402
from rig_walker import fit_height_and_ground  # noqa: E402

# M_PlasticDark의 바탕색(선형). 손님과 같은 젖은 검정이다.
WET_BLACK = (0.020, 0.022, 0.025)
# 원본 거칠기(0.5~1)를 0.32~0.40으로 옮긴다. 젖은 표면이 손전등을 조금 되쏜다.
ROUGH_SCALE, ROUGH_OFFSET = 0.16, 0.24
# 게임이 숙이는 최대 각도(IGShadow::MaxLeanDegrees)와 팔이 앞으로 나오는 비율. 미리보기용.
PREVIEW_LEAN_DEGREES = 28.0
PREVIEW_REACH_RATIO = 0.2


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--glb", required=True)
    parser.add_argument("--name", default="SK_Eoduksini")
    parser.add_argument("--height", type=float, default=200.0, help="키 cm. AIGShadowFigure의 기본 몸이 2 m다")
    parser.add_argument("--yaw", type=float, default=90.0, help="정면을 +X로 돌리는 Z 회전(도)")
    parser.add_argument("--waist", type=float, default=1.08, help="허리 높이(m). 천 자락이 시작되는 곳에서 숙인다")
    parser.add_argument("--waist-band", type=float, default=0.10, help="허리 위아래로 root와 spine을 섞는 폭(m)")
    parser.add_argument("--voxel-remesh", type=float, default=0.004)
    parser.add_argument("--budget", type=int, default=12000)
    parser.add_argument("--texture-size", type=int, default=2048)
    parser.add_argument("--out", default=None)
    parser.add_argument("--no-bake", action="store_true")
    parser.add_argument("--notes", default="")
    return parser.parse_args(argv)


# --------------------------------------------------------------------------
# 재질
# --------------------------------------------------------------------------

def _srgb_to_linear(values):
    import numpy as np
    return np.where(values <= 0.04045, values / 12.92, ((values + 0.055) / 1.055) ** 2.4)


def wet_black(ob):
    """점토색을 젖은 검정으로 바꾼다. 굽기가 이 색과 거칠기를 옮긴다."""
    import numpy as np
    for mat in ob.data.materials:
        if mat is None or not mat.use_nodes:
            continue
        tree, nodes, links = ig._nodes(mat)
        bsdf = ig._principled(mat)
        base = bsdf.inputs["Base Color"]
        level = 0.45
        if base.is_linked and base.links[0].from_node.type == "TEX_IMAGE" and base.links[0].from_node.image:
            image = base.links[0].from_node.image
            pixels = np.empty(image.size[0] * image.size[1] * 4, dtype=np.float32)
            image.pixels.foreach_get(pixels)
            rgb = pixels.reshape(-1, 4)[:, :3]
            if not image.is_float:
                rgb = _srgb_to_linear(rgb)
            luminance = rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
            # 아틀라스의 빈 바탕은 0이다. 칠해진 곳만 잰다.
            painted = luminance[luminance > 0.01]
            if len(painted):
                level = float(np.median(painted))
        if base.is_linked:
            source = base.links[0].from_socket
            scale = nodes.new("ShaderNodeVectorMath")
            scale.operation = "MULTIPLY"
            scale.inputs[1].default_value = tuple(c / max(level, 1e-3) for c in WET_BLACK)
            links.new(source, scale.inputs[0])
            links.new(scale.outputs["Vector"], base)
        else:
            base.default_value = (*WET_BLACK, 1.0)
        rough = bsdf.inputs["Roughness"]
        if rough.is_linked:
            remap = nodes.new("ShaderNodeMath")
            remap.operation = "MULTIPLY_ADD"
            remap.inputs[1].default_value = ROUGH_SCALE
            remap.inputs[2].default_value = ROUGH_OFFSET
            links.new(rough.links[0].from_socket, remap.inputs[0])
            links.new(remap.outputs[0], rough)
        else:
            rough.default_value = ROUGH_OFFSET + ROUGH_SCALE * float(rough.default_value)
        ig.log(f"  wet black: source level {level:.3f} -> {WET_BLACK}, roughness x{ROUGH_SCALE}+{ROUGH_OFFSET}")


# --------------------------------------------------------------------------
# 뼈 자리
# --------------------------------------------------------------------------

SLICE = 0.02


def find_landmarks(ob, waist_z):
    """허리·목·어깨·손끝(미터, 앞 +X, 위 +Z). 팔은 몸통과의 틈으로 가른다."""
    import numpy as np
    pts = vertex_array(ob)
    height = float(pts[:, 2].max())
    gaps = []
    for z0 in np.arange(0.25 * height, 0.85 * height, SLICE):
        band = pts[(pts[:, 2] >= z0) & (pts[:, 2] < z0 + SLICE)]
        if len(band) < 30:
            continue
        ay = np.sort(np.abs(band[:, 1]))
        best_gap, best_mid = 0.0, 0.0
        for a, b in zip(ay[:-1], ay[1:]):
            if 0.04 < a < 0.40 and b - a > best_gap:
                best_gap, best_mid = float(b - a), float(a + b) * 0.5
        # 틈 바깥에 팔이 실제로 있어야 한다(천 자락의 주름 틈이 아니라).
        outside = np.count_nonzero(ay > best_mid)
        if best_gap > 0.008 and outside >= 12:
            gaps.append((float(z0 + SLICE * 0.5), best_mid))
    if not gaps:
        raise RuntimeError("팔과 몸통 사이 틈을 못 찾았다")
    # 겨드랑이에서 손끝까지 이어진 가장 긴 띠만 팔로 본다.
    runs, current = [], [gaps[0]]
    for item in gaps[1:]:
        if item[0] - current[-1][0] <= SLICE * 1.6:
            current.append(item)
        else:
            runs.append(current)
            current = [item]
    runs.append(current)
    arm_run = max(runs, key=len)
    hand_bottom = arm_run[0][0] - SLICE * 0.5
    armpit = arm_run[-1][0] + SLICE * 0.5

    def gap_at(z):
        if z <= arm_run[0][0]:
            return arm_run[0][1]
        for (z0, g0), (z1, g1) in zip(arm_run[:-1], arm_run[1:]):
            if z0 <= z <= z1:
                return g0 + (g1 - g0) * (z - z0) / max(z1 - z0, 1e-6)
        return arm_run[-1][1]

    def core_center_x(z0, z1):
        # 앞뒤 끝의 가운데. 중앙값은 앞섶 주름에 점이 몰려 앞으로 쏠린다.
        m = (pts[:, 2] > z0) & (pts[:, 2] < z1)
        sel = pts[m]
        sel = sel[np.abs(sel[:, 1]) < gap_at((z0 + z1) * 0.5) - 0.01]
        if not len(sel):
            return 0.0
        return float(np.percentile(sel[:, 0], 3) + np.percentile(sel[:, 0], 97)) * 0.5

    neck_z = 0.86 * height
    waist = Vector((core_center_x(waist_z - 0.03, waist_z + 0.03), 0.0, waist_z))
    neck = Vector((core_center_x(neck_z - 0.03, neck_z + 0.03), 0.0, neck_z))
    arms = {}
    for side, sign in (("l", 1.0), ("r", -1.0)):
        sel = pts[(np.sign(pts[:, 1]) == sign) & (pts[:, 2] > hand_bottom - 0.01) & (pts[:, 2] < armpit)]
        sel = sel[np.abs(sel[:, 1]) > np.array([gap_at(z) for z in sel[:, 2]])]
        top = sel[sel[:, 2] > armpit - 0.10]
        bottom = sel[sel[:, 2] < hand_bottom + 0.08]
        # 어깨 관절은 겨드랑이 조금 위, 팔 윗단 단면의 가운데다.
        shoulder = Vector((float(np.median(top[:, 0])), sign * float(np.median(np.abs(top[:, 1]))), armpit + 0.05))
        tip = Vector((float(np.median(bottom[:, 0])), sign * float(np.median(np.abs(bottom[:, 1]))), hand_bottom))
        arms[side] = (shoulder, tip)
    marks = {"height": height, "waist": waist, "neck": neck, "arms": arms,
             "armpit": armpit, "hand_bottom": hand_bottom, "gap_at": gap_at}
    ig.log(f"eoduksini landmarks: height={height:.3f} waist={tuple(round(v, 3) for v in waist)} "
           f"neck={tuple(round(v, 3) for v in neck)} armpit={armpit:.3f} hand_bottom={hand_bottom:.3f}")
    for side in ("l", "r"):
        s, t = arms[side]
        ig.log(f"  arm_{side}: shoulder={tuple(round(v, 3) for v in s)} tip={tuple(round(v, 3) for v in t)}")
    return marks


def build_armature(name, marks):
    arm_data = bpy.data.armatures.new(name + "_Armature")
    arm = ig._link(bpy.data.objects.new(name + "_Armature", arm_data))
    ig.set_active(arm)
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm_data.edit_bones

    def bone(bname, head, tail, parent=None):
        b = eb.new(bname)
        b.head = Vector(head)
        b.tail = Vector(tail)
        if parent is not None:
            b.parent = eb[parent]
            b.use_connect = False
        b.align_roll(Vector((1.0, 0.0, 0.0)))
        return b

    bone("root", (0.0, 0.0, 0.0), (0.0, 0.0, 0.10))
    bone("spine", marks["waist"], marks["neck"], "root")
    for side in ("l", "r"):
        shoulder, tip = marks["arms"][side]
        bone(f"arm_{side}", shoulder, tip, "spine")
    bpy.ops.object.mode_set(mode="OBJECT")
    for pb in arm.pose.bones:
        pb.rotation_mode = "XYZ"
    return arm


# --------------------------------------------------------------------------
# 웨이트
# --------------------------------------------------------------------------

def _smoothstep(edge0, edge1, x):
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def skin(mesh_ob, arm, marks, waist_band):
    """허리 띠에서 root·spine을, 어깨에서 spine·팔을 섞는다."""
    import numpy as np
    me = mesh_ob.data
    count = len(me.vertices)
    co = np.empty(count * 3, dtype=np.float32)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    armpit = marks["armpit"]
    hand_bottom = marks["hand_bottom"]
    gap_at = marks["gap_at"]
    waist_z = marks["waist"].z
    arm_w = {"l": np.zeros(count, dtype=np.float32), "r": np.zeros(count, dtype=np.float32)}
    is_arm = np.zeros(count, dtype=bool)
    for i, (x, y, z) in enumerate(co):
        side = "l" if y >= 0.0 else "r"
        ay = abs(y)
        if hand_bottom - 0.03 < z < armpit and ay > gap_at(z):
            is_arm[i] = True
            # 겨드랑이 8 cm 아래부터 어깨로 가며 팔뼈 몫이 줄어든다.
            arm_w[side][i] = 1.0 - 0.4 * _smoothstep(armpit - 0.08, armpit, z)
        elif armpit <= z < armpit + 0.14:
            # 어깨 머리. 바깥쪽일수록, 겨드랑이에 가까울수록 팔을 따른다.
            outward = _smoothstep(0.07, 0.12, ay)
            arm_w[side][i] = 0.6 * outward * (1.0 - _smoothstep(armpit, armpit + 0.14, z))
    # 이웃끼리 몇 번 고르게 펴서 어깨의 경계를 지운다. 팔과 몸통은 겨드랑이 아래로
    # 떨어져 있어 손이 몸통 웨이트를 끌고 오지 않는다.
    edges = np.empty(len(me.edges) * 2, dtype=np.int32)
    me.edges.foreach_get("vertices", edges)
    edges = edges.reshape(-1, 2)
    neighbors = [[] for _ in range(count)]
    for a, b in edges:
        neighbors[a].append(b)
        neighbors[b].append(a)
    shoulder_zone = np.where((co[:, 2] > armpit - 0.16) & (co[:, 2] < armpit + 0.18))[0]
    for _ in range(6):
        for side in ("l", "r"):
            w = arm_w[side]
            smoothed = w.copy()
            for i in shoulder_zone:
                ns = neighbors[i]
                if ns:
                    smoothed[i] = 0.5 * w[i] + 0.5 * float(np.mean(w[ns]))
            arm_w[side] = smoothed
    groups = {name: mesh_ob.vertex_groups.new(name=name) for name in ("root", "spine", "arm_l", "arm_r")}
    for i in range(count):
        wl, wr = float(arm_w["l"][i]), float(arm_w["r"][i])
        rest = max(0.0, 1.0 - wl - wr)
        # 팔은 허리 아래로 내려와 있어도 몸통 위쪽(spine)에 달려 있다.
        up = 1.0 if is_arm[i] else _smoothstep(waist_z - waist_band, waist_z + waist_band, float(co[i, 2]))
        weights = {"root": rest * (1.0 - up), "spine": rest * up, "arm_l": wl, "arm_r": wr}
        total = sum(weights.values())
        for name, w in weights.items():
            if w > 1e-4:
                groups[name].add([i], w / total, "REPLACE")
    mod = mesh_ob.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    mod.use_vertex_groups = True
    mod.use_deform_preserve_volume = False
    mesh_ob.parent = arm
    ig.log(f"  skin: waist {waist_z:.3f}±{waist_band:.2f}, arm vertices {int(is_arm.sum())}, "
           f"shoulder smoothing over {len(shoulder_zone)} vertices")


# --------------------------------------------------------------------------
# 미리보기 자세 — 게임의 AIGShadowFigure::ApplyPose와 같은 계산
# --------------------------------------------------------------------------

def pose_lean(arm, lean_deg, reach_ratio):
    """허리를 숙이고 팔은 거의 늘어진 채로 둔다."""
    bpy.context.view_layer.update()
    spine_rest = arm.data.bones["spine"].matrix_local.copy()
    pivot = spine_rest.translation.copy()
    # +Y 축으로 돌리면 위가 +X(앞)로 숙는다.
    bend = Matrix.Translation(pivot) @ Matrix.Rotation(math.radians(lean_deg), 4, "Y") @ Matrix.Translation(-pivot)
    arm.pose.bones["spine"].matrix = bend @ spine_rest
    bpy.context.view_layer.update()
    for side in ("l", "r"):
        rest = arm.data.bones[f"arm_{side}"].matrix_local.copy()
        # 아래로 늘어진 팔을 -Y 축으로 돌리면 손끝이 앞으로 나온다.
        posed = Matrix.Rotation(math.radians(-lean_deg * reach_ratio), 4, "Y") @ rest.to_3x3().to_4x4()
        posed.translation = bend @ rest.translation
        arm.pose.bones[f"arm_{side}"].matrix = posed
        bpy.context.view_layer.update()


def clear_pose(arm):
    for pb in arm.pose.bones:
        pb.location = (0.0, 0.0, 0.0)
        pb.rotation_euler = (0.0, 0.0, 0.0)
        pb.scale = (1.0, 1.0, 1.0)
    bpy.context.view_layer.update()


def export_rig_fbx(path, arm, mesh_ob):
    """동작 없이 뼈와 메시만 내보낸다. 동작을 구우면 Unreal이 쓸 데 없는 AnimSequence를 만든다."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.context.view_layer.update()
    for other in list(bpy.context.view_layer.objects):
        other.select_set(False)
    arm.select_set(True)
    mesh_ob.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        object_types={"ARMATURE", "MESH"},
        use_mesh_modifiers=True,
        mesh_smooth_type="EDGE",
        use_tspace=True,
        add_leaf_bones=False,
        primary_bone_axis="Y",
        secondary_bone_axis="X",
        armature_nodetype="NULL",
        use_armature_deform_only=True,
        bake_anim=False,
        path_mode="STRIP",
        embed_textures=False,
        global_scale=1.0,
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_NONE",
        use_custom_props=False,
        **ig.EXPORT_AXIS)
    ig.log(f"  fbx(skeletal, no animation) -> {path} ({os.path.getsize(path)} bytes)")


def main():
    args = parse_args()
    out_root = os.path.abspath(args.out) if args.out else os.path.abspath(
        os.path.join(HERE, "..", "..", "Content", "SourceArt", "Blender"))
    name = args.name
    out_dir = os.path.join(out_root, name)
    os.makedirs(out_dir, exist_ok=True)
    ig.reset_scene()

    high = import_glb(os.path.abspath(args.glb))
    ig.log(f"{name}: imported {ig.triangle_count(high)} tris")
    if args.yaw:
        bake_rotation(high, 0.0, 0.0, args.yaw)
    scale = fit_height_and_ground(high, args.height)
    source_mesh = bmesh.new()
    source_mesh.from_mesh(high.data)
    bmesh.ops.remove_doubles(source_mesh, verts=list(source_mesh.verts), dist=.00005)
    bmesh.ops.recalc_face_normals(source_mesh, faces=list(source_mesh.faces))
    source_mesh.to_mesh(high.data)
    source_mesh.free()
    ig.prepare_organic_source(high, roughness_floor=0.0)
    wet_black(high)
    lo, hi = ig.bounds(high)
    ig.log(f"{name}: scale x{scale:.4f} -> {(hi - lo).x * 100:.1f} x {(hi - lo).y * 100:.1f} x {(hi - lo).z * 100:.1f} cm")

    low = high.copy()
    low.data = high.data.copy()
    low.name = name
    low.data.name = name
    ig._link(low)
    if args.voxel_remesh > 0.0:
        mod = low.modifiers.new("Remesh", "REMESH")
        mod.mode = "VOXEL"
        mod.voxel_size = args.voxel_remesh
        mod.use_smooth_shade = True
        ig.apply_modifiers(low)
        ig.log(f"{name}: voxel remesh {args.voxel_remesh} m -> {ig.triangle_count(low)} tris")
    ig.remove_small_islands(low, keep_largest=False)
    ig.decimate(low, args.budget)
    for poly in low.data.polygons:
        poly.use_smooth = True
    low.data.materials.clear()
    ue_bounds = ig.bounds(low)

    low.hide_render = True
    ig.render_preview(high, os.path.join(out_dir, f"{name}_source_preview.png"))
    low.hide_render = False

    ig.mirror_y(low)
    ig.mirror_y(high)
    # 틈은 고밀도 원본으로 잰다. 줄인 메시는 2 cm 단면에 점이 몇 십 개라 어디서나 틈이 보인다.
    marks = find_landmarks(high, args.waist)
    textures = {}
    if not args.no_bake:
        ig.uv_smart(low, margin=0.003)
        extrusion = max(args.voxel_remesh * 2.0, 0.006)
        textures = ig.bake_from_high(low, high, name, out_dir, size=args.texture_size,
                                     cage_extrusion=extrusion, max_ray_distance=extrusion * 3.0)
    high_mesh = high.data
    bpy.data.objects.remove(high, do_unlink=True)
    bpy.data.meshes.remove(high_mesh)

    if textures:
        ig.preview_material_from_bakes(low, textures)
    else:
        mat = bpy.data.materials.new("__flat")
        bsdf = ig._principled(mat)
        bsdf.inputs["Base Color"].default_value = (0.32, 0.36, 0.44, 1.0)
        bsdf.inputs["Roughness"].default_value = 0.9
        ig.assign_material(low, mat)

    arm = build_armature(name, marks)
    skin(low, arm, marks, args.waist_band)

    props, pmat = debug_bone_props(arm)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_bones.png"), extra_objects=props,
                      camera_yaw_deg=-35.0, camera_pitch_deg=10.0)
    remove_props(props, pmat)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_preview.png"), camera_yaw_deg=-20.0)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_preview_torch.png"), flashlight=True, camera_yaw_deg=-40.0)
    pose_lean(arm, PREVIEW_LEAN_DEGREES, PREVIEW_REACH_RATIO)
    # 카메라 yaw 0은 -Y에서 본다. 앞(+X)이 화면 오른쪽인 옆모습이다.
    ig.render_preview(low, os.path.join(out_dir, f"{name}_lean_side.png"), camera_yaw_deg=0.0, camera_pitch_deg=4.0)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_lean_front.png"), camera_yaw_deg=-70.0, camera_pitch_deg=4.0)
    ig.render_preview(low, os.path.join(out_dir, f"{name}_lean_back.png"), camera_yaw_deg=110.0, camera_pitch_deg=8.0)
    clear_pose(arm)

    if args.no_bake:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, f"{name}.blend"))
        ig.log(f"{name}: previews only (--no-bake)")
        return

    slots = ig.finalize_slots(low)
    fbx_path = os.path.join(out_dir, f"{name}.fbx")
    export_rig_fbx(fbx_path, arm, low)
    ig.write_manifest(out_dir, name, "hero", fbx_path, textures, low, slots,
                      notes=args.notes or f"생성 GLB {os.path.basename(args.glb)}에서 허리를 숙이는 뼈 넷으로 리깅",
                      origin="bottom", ue_bounds=ue_bounds)
    manifest_path = os.path.join(out_dir, "manifest.json")
    with open(manifest_path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    data["skeletal"] = True
    data["animations"] = {}
    data["bones"] = {"waist_cm": round(marks["waist"].z * 100.0, 1),
                     "armpit_cm": round(marks["armpit"] * 100.0, 1)}
    data["generated_from"] = os.path.relpath(os.path.abspath(args.glb), out_root).replace("\\", "/")
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, f"{name}.blend"))
    ig.log(f"{name}: done tris={ig.triangle_count(low)} bones={len(arm.data.bones)}")


if __name__ == "__main__":
    main()
