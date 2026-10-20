"""팔을 벌려 뽑은 근무복 인물에 호흡·고개와 두 팔을 붙인다.

    blender -b --factory-startup --python Scripts/blender/rig_store_clerk.py -- [<out_dir>] [--review <폴더>]

몸통·머리 두 마디만 있던 리그에 팔 셋(위팔·아래팔·손)을 좌우로 더한다(2026-10-19). 단말기를 누르고
프린터에서 영수증을 뜯어 손님 앞에 놓는 손, 그리고 서 있을 때 내린 팔은 게임(AIGStoreClerk)이 이 뼈를
두 관절 IK로 움직인다. 몸은 -Y(손님 쪽)를 보고, 오른팔은 -X 쪽이다.

예전 몸은 팔을 내린 시안으로 뽑아 팔 안쪽과 손이 조끼·바지에 한 면으로 붙어 있었다. 그래서 몸은 팔을
22도 벌린 그림(Scripts/make_narin_arms_out.py)으로 다시 뽑았다(rebuild_clerk_head.py). 팔 축은 메시에서
찾는다. 어깨 끝에서 5 cm 안쪽이 어깨 관절이고, 몸통과 틈이 벌어지는 높이마다 팔 단면의 가운데를
잇는다. 팔 끝 쪽으로 가장 먼 정점이 손끝이다. 팔꿈치와 손목은 어깨에서 손끝까지 잇는 꺾인 선의
40%·76% 자리다. 팔 정점은 손 쪽에서 시작해 그 선 가까이만 이웃을 따라 모으고, 어깨 관절 둘레는 거리에
따라 몸통과 섞는다.
--review는 팔을 내린 자세와 단말기·프린터·상판 자세로 삼각형이 얼마나 늘어나는지 재고 찍는다.
"""
import argparse
import json
import math
import os
import sys

import bpy
import numpy as np
from collections import deque
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ig_blender_lib as ig
from rig_crawler import new_action, key_rot, cyclic, export_skeletal_fbx
from rig_walker import solve_limb

SHOULDER_HEIGHT = 1.275
ELBOW_AT = 0.40
WRIST_AT = 0.76
# 팔로 모을 정점: 팔 축에서 이 거리 안. 어깨 관절에서 축을 따라 이만큼 내려간 데부터 팔이다.
ARM_RADIUS = 0.085
ARM_START = 0.07
# 어깨 관절 둘레. 이 안에서는 몸통과 위팔을 거리에 따라 섞는다.
SHOULDER_INNER = 0.05
SHOULDER_OUTER = 0.13
# 검수 자세(몸 기준 m). 손목이 갈 자리와 팔꿈치가 향할 쪽. hang은 서 있을 때 내린 팔이다.
REVIEW_POSES = (
    ("hang", "R", Vector((-0.235, 0.02, 0.80)), Vector((-0.40, 0.20, 1.05))),
    ("tap_right", "R", Vector((-0.05, -0.42, 1.18)), Vector((-0.45, 0.10, 0.90))),
    ("tap_left", "L", Vector((0.08, -0.42, 1.18)), Vector((0.45, 0.10, 0.90))),
    ("printer_right", "R", Vector((-0.20, -0.36, 0.98)), Vector((-0.45, 0.05, 0.80))),
    ("place_right", "R", Vector((-0.10, -0.50, 0.92)), Vector((-0.45, 0.05, 0.75))),
)


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("out", nargs="?", default=None)
    parser.add_argument("--review", default=None)
    return parser.parse_args(argv)


def smoothstep(edge0, edge1, value):
    t = max(0.0, min(1.0, (value - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def polyline_point(points, fraction):
    """꺾인 선 위 호 길이 비율 fraction의 점."""
    lengths = [(b - a).length for a, b in zip(points, points[1:])]
    target = fraction * sum(lengths)
    for a, b, length in zip(points, points[1:], lengths):
        if target <= length:
            return a.lerp(b, target / max(length, 1e-9))
        target -= length
    return points[-1].copy()


def find_arms(mesh):
    """좌우 팔의 어깨·팔꿈치·손목·손끝(m). 어깨 관절은 어깨 끝에서 5 cm 안쪽, 팔은 높이별 단면의 가운데를 잇는다."""
    points = np.array([list(mesh.matrix_world @ v.co) for v in mesh.data.vertices])
    arms = {}
    for side, sign in (("R", -1.0), ("L", 1.0)):
        reach = sign * points[:, 0]
        top = points[(reach > 0.10) & (points[:, 2] > 1.25) & (points[:, 2] < 1.31)]
        top_reach = sign * top[:, 0]
        shoulder = Vector((sign * (float(np.percentile(top_reach, 98)) - 0.05), float(np.median(top[:, 1])), SHOULDER_HEIGHT))
        # 위에서 아래로 단면을 내려가며 팔을 따라간다. 앞 단면의 가운데에서 7 cm 안에 있는 덩어리만 팔로
        # 치고, 두 단면 연달아 못 찾으면 팔이 끝난 것이다. 그러지 않으면 손 아래 바지 단면을 팔로 잡아
        # 손끝이 발밑까지 내려갔다.
        centers = []
        misses = 0
        for z0 in np.arange(1.13, 0.55, -0.03):
            band = points[(points[:, 2] >= z0) & (points[:, 2] < z0 + 0.03) & (reach > 0.12)]
            found = None
            if len(band) >= 12:
                band_reach = np.sort(sign * band[:, 0])
                gaps = np.diff(band_reach)
                window = (band_reach[:-1] > 0.14) & (band_reach[:-1] < 0.42)
                if window.any():
                    index = int(np.argmax(np.where(window, gaps, -1.0)))
                    if gaps[index] >= 0.012:
                        arm = band[sign * band[:, 0] > band_reach[index] + gaps[index] * 0.5]
                        if len(arm) >= 6:
                            found = Vector(arm.mean(axis=0).tolist())
            reference = centers[-1] if centers else shoulder
            limit = 0.07 if centers else 0.22
            if found is not None and (found - reference).length < limit:
                centers.append(found)
                misses = 0
            elif centers:
                misses += 1
                if misses >= 2:
                    break
        if len(centers) < 4:
            raise RuntimeError(f"{side} 팔 단면을 못 찾았다")
        # 손끝: 마지막 단면 둘레 15 cm 안에서, 마지막 단면들이 가리키는 쪽으로 가장 먼 정점.
        direction = (centers[-1] - centers[-3]).normalized()
        near = points[np.linalg.norm(points - np.array(list(centers[-1])), axis=1) < 0.15]
        near = near[(near[:, 2] < centers[-1].z + 0.02) & ((sign * near[:, 0]) > 0.2)]
        far = max((Vector(p.tolist()) for p in near), key=lambda p: (p - centers[-1]).dot(direction))
        chain = [shoulder] + centers + [far]
        length = sum((b - a).length for a, b in zip(chain, chain[1:]))
        arms[side] = {
            "shoulder": shoulder,
            "elbow": polyline_point(chain, ELBOW_AT),
            "wrist": polyline_point(chain, WRIST_AT),
            "tip": far,
            "chain": chain,
            "length": length,
        }
        ig.log('STORE_CLERK_ARM %s shoulder=%s elbow=%s wrist=%s tip=%s length=%.3f slices=%d' % (
            side, tuple(round(v, 3) for v in shoulder), tuple(round(v, 3) for v in arms[side]["elbow"]),
            tuple(round(v, 3) for v in arms[side]["wrist"]), tuple(round(v, 3) for v in far), length, len(centers)))
    return arms


def chain_parameter(co, a):
    """co를 팔 꺾인 선(어깨·팔꿈치·손목·손끝)에 내렸을 때의 호 길이 비율과 거리."""
    joints = [a["shoulder"], a["elbow"], a["wrist"], a["tip"]]
    lengths = [(q - p).length for p, q in zip(joints, joints[1:])]
    total = sum(lengths)
    best = (float("inf"), 0.0)
    run = 0.0
    for p, q, length in zip(joints, joints[1:], lengths):
        ab = q - p
        t = max(0.0, min(1.0, (co - p).dot(ab) / max(ab.length_squared, 1e-12)))
        d = (co - (p + ab * t)).length
        if d < best[0]:
            best = (d, (run + t * length) / total)
        run += length
    return best[1], best[0]


def build_armature(name, arms):
    arm_data = bpy.data.armatures.new(name + "Rig")
    arm = bpy.data.objects.new(name + "Rig", arm_data)
    bpy.context.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bones = [
        ('Root', (0, 0, 0), (0, 0, .85), None),
        ('Torso', (0, 0, .85), (0, 0, 1.35), 'Root'),
        ('Head', (0, 0, 1.35), (0, 0, 1.62), 'Torso'),
    ]
    for side in ("R", "L"):
        a = arms[side]
        # 손끝 뼈는 가중치가 없다. 언리얼은 끝 뼈의 꼬리를 모르니, 손가락 끝 자리를 여기서 읽는다.
        tip_axis = (a["tip"] - a["wrist"]).normalized()
        bones += [
            (f'UpperArm_{side}', a["shoulder"], a["elbow"], 'Torso'),
            (f'LowerArm_{side}', a["elbow"], a["wrist"], f'UpperArm_{side}'),
            (f'Hand_{side}', a["wrist"], a["tip"], f'LowerArm_{side}'),
            (f'Fingertip_{side}', a["tip"], a["tip"] + tip_axis * 0.02, f'Hand_{side}'),
        ]
    for bone_name, start, end, parent in bones:
        bone = arm_data.edit_bones.new(bone_name)
        bone.head = start
        bone.tail = end
        if parent:
            bone.parent = arm_data.edit_bones[parent]
            bone.use_connect = parent.startswith(("UpperArm", "LowerArm", "Hand"))
    bpy.ops.object.mode_set(mode='OBJECT')
    return arm


def body_weights(co):
    torso = max(0., min(1., (co.z - .82) / .23))
    head = max(0., min(1., (co.z - 1.34) / .12))
    return {'Root': 1. - torso, 'Torso': torso * (1. - head), 'Head': head}


def arm_members(mesh, arms):
    """팔 정점 → 쪽. 손 쪽에서 시작해 팔 축 가까이만 이웃을 따라 모은다."""
    verts = [mesh.matrix_world @ v.co for v in mesh.data.vertices]
    neighbors = [[] for _ in verts]
    for edge in mesh.data.edges:
        a, b = edge.vertices
        neighbors[a].append(b)
        neighbors[b].append(a)
    members = {}
    for side, a in arms.items():
        def candidate(co):
            fraction, distance = chain_parameter(co, a)
            if (co.x < 0.0) != (side == "R"):
                return False
            return fraction * a["length"] > ARM_START and distance < ARM_RADIUS

        ok = [candidate(co) for co in verts]
        seen = [False] * len(verts)
        queue = deque(i for i, co in enumerate(verts) if ok[i] and chain_parameter(co, a)[0] > 0.6)
        for i in queue:
            seen[i] = True
        while queue:
            i = queue.popleft()
            for j in neighbors[i]:
                if ok[j] and not seen[j]:
                    seen[j] = True
                    queue.append(j)
        for i, flag in enumerate(seen):
            if flag:
                members[i] = side
    return members


def weights_for(co, arm_side, arms):
    side = "R" if co.x < 0.0 else "L"
    a = arms[side]
    away = (co - a["shoulder"]).length
    if arm_side is None:
        if away >= SHOULDER_OUTER:
            return body_weights(co)
        # 어깨 둥근 데. 관절에 가까울수록 위팔을 따른다.
        share = 1.0 - smoothstep(SHOULDER_INNER, SHOULDER_OUTER, away)
        weights = {bone: value * (1.0 - share) for bone, value in body_weights(co).items()}
        weights[f'UpperArm_{side}'] = share
        return weights
    t, _ = chain_parameter(co, a)
    upper = 1.0 - smoothstep(ELBOW_AT - 0.06, ELBOW_AT + 0.06, t)
    hand = smoothstep(WRIST_AT - 0.035, WRIST_AT + 0.035, t)
    weights = {
        f'UpperArm_{arm_side}': upper,
        f'LowerArm_{arm_side}': (1.0 - upper) * (1.0 - hand),
        f'Hand_{arm_side}': (1.0 - upper) * hand,
    }
    if away < SHOULDER_OUTER:
        # 겨드랑이 쪽 팔 위끝도 어깨 둥근 데와 같은 식으로 몸통과 섞는다.
        share = 1.0 - smoothstep(SHOULDER_INNER, SHOULDER_OUTER, away)
        torso_share = (1.0 - share) * (1.0 - smoothstep(0.0, ARM_START + 0.05, t * a["length"]))
        weights = {bone: value * (1.0 - torso_share) for bone, value in weights.items()}
        weights['Torso'] = torso_share
    return weights


def skin(mesh, arm, members, arms):
    mesh.vertex_groups.clear()
    groups = {bone.name: mesh.vertex_groups.new(name=bone.name) for bone in arm.data.bones}
    counts = {name: 0 for name in groups}
    for vertex in mesh.data.vertices:
        weights = weights_for(mesh.matrix_world @ vertex.co, members.get(vertex.index), arms)
        total = sum(weights.values()) or 1.0
        for bone, weight in weights.items():
            if weight / total > 1e-4:
                groups[bone].add([vertex.index], weight / total, 'REPLACE')
                counts[bone] += 1
    modifier = mesh.modifiers.new('근무 중 호흡', 'ARMATURE')
    modifier.object = arm
    mesh.parent = arm
    return counts


def author_idle(arm):
    for bone in arm.pose.bones:
        bone.rotation_mode = 'XYZ'
    bpy.context.scene.render.fps = 30
    action = new_action(arm, 'Idle', 180)
    for frame in range(0, 181, 6):
        phase = frame / 180 * math.tau
        key_rot(arm, 'Torso', frame, rx=.22 * math.sin(phase * 2), rz=.18 * math.sin(phase))
        key_rot(arm, 'Head', frame, rx=.7 * math.sin(phase), ry=1.7 * math.sin(phase))
    cyclic(arm, action)
    return action


def triangle_areas(mesh):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = mesh.evaluated_get(depsgraph)
    data = evaluated.to_mesh()
    data.calc_loop_triangles()
    areas = [tri.area for tri in data.loop_triangles]
    evaluated.to_mesh_clear()
    return areas


def render_closeup(path, target, offset):
    """target을 offset만큼 떨어진 자리에서 가까이 찍는다. 손 모양과 어깨·옆구리 이음을 보는 용도다."""
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x, scene.render.resolution_y = 900, 900
    scene.render.resolution_percentage = 100
    cam_data = bpy.data.cameras.new("__close_cam")
    cam_data.lens = 50
    cam = bpy.data.objects.new("__close_cam", cam_data)
    bpy.context.collection.objects.link(cam)
    cam.location = target + offset
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    world = bpy.data.worlds.new("__close_world")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.35, 0.36, 0.38, 1.0)
    scene.world = world
    lights = []
    for index, (light_offset, energy) in enumerate(((Vector((-1.0, -1.2, 1.4)), 190.0),
                                                    (Vector((1.4, -0.6, 0.9)), 70.0),
                                                    (Vector((0.3, 1.3, 1.2)), 50.0))):
        light_data = bpy.data.lights.new(f"__close_key{index}", "AREA")
        light_data.energy = energy
        light_data.size = 0.6
        light = bpy.data.objects.new(f"__close_key{index}", light_data)
        bpy.context.collection.objects.link(light)
        light.location = target + light_offset
        light.rotation_euler = (target - light.location).to_track_quat("-Z", "Y").to_euler()
        lights.append(light)
    scene.render.filepath = path
    scene.render.image_settings.file_format = "PNG"
    bpy.ops.render.render(write_still=True)
    for ob in lights + [cam]:
        data = ob.data
        bpy.data.objects.remove(ob, do_unlink=True)
        if isinstance(data, bpy.types.Light):
            bpy.data.lights.remove(data)
        else:
            bpy.data.cameras.remove(data)
    bpy.data.worlds.remove(world)


def review(mesh, arm, folder):
    os.makedirs(folder, exist_ok=True)

    def reset_pose():
        arm.animation_data.action = None
        for bone in arm.pose.bones:
            bone.rotation_mode = 'XYZ'
            bone.rotation_euler = (0.0, 0.0, 0.0)
            bone.location = (0.0, 0.0, 0.0)
        bpy.context.view_layer.update()

    reset_pose()
    rest = triangle_areas(mesh)
    for yaw, tag in ((180, 'front'), (90, 'side')):
        ig.render_preview(mesh, os.path.join(folder, f'rest_{tag}.png'), camera_yaw_deg=yaw, camera_pitch_deg=8)
    report = {}
    for name, side, target, pole in REVIEW_POSES:
        reset_pose()
        action = new_action(arm, 'Review_' + name, 1)
        hang = name == "hang"
        # 내린 팔은 양쪽을 같이 내린다. 손끝은 아래로 둔다.
        limbs = [(side, target, pole)]
        if hang:
            limbs.append(("L", Vector((-target.x, target.y, target.z)), Vector((-pole.x, pole.y, pole.z))))
        for limb_side, limb_target, limb_pole in limbs:
            solve_limb(arm, f'UpperArm_{limb_side}', f'LowerArm_{limb_side}', f'Hand_{limb_side}', limb_target, limb_pole, 0,
                       end_direction=Vector((0.0, 0.0, -1.0)) if hang else Vector((0.0, -0.6, -0.8)))
        bpy.context.scene.frame_set(0)
        bpy.context.view_layer.update()
        areas = triangle_areas(mesh)
        ratios = sorted(a / r for a, r in zip(areas, rest) if r > 1e-9)
        report[name] = {
            "max": round(ratios[-1], 2),
            "p999": round(ratios[int(len(ratios) * 0.999)], 2),
            "p99": round(ratios[int(len(ratios) * 0.99)], 2),
            "over2": sum(1 for value in ratios if value > 2.0),
        }
        for yaw, tag in ((180, 'front'), (90, 'side'), (135, 'three_quarter')):
            ig.render_preview(mesh, os.path.join(folder, f'{name}_{tag}.png'), camera_yaw_deg=yaw, camera_pitch_deg=8)
        hand_world = arm.matrix_world @ arm.pose.bones[f'Hand_{side}'].head
        outward = Vector((-1.0 if side == "R" else 1.0, 0.0, 0.0))
        render_closeup(os.path.join(folder, f'{name}_hand.png'), hand_world, Vector((0.0, -0.40, 0.10)) + outward * 0.15)
        render_closeup(os.path.join(folder, f'{name}_side_body.png'), Vector((0.20 * outward.x, 0.0, 1.10)),
                       outward * 0.70 + Vector((0.0, -0.25, 0.05)))
        arm.animation_data.action = None
        bpy.data.actions.remove(action)
    with open(os.path.join(folder, 'stretch.json'), 'w', encoding='utf-8') as handle:
        json.dump(report, handle, ensure_ascii=False, indent=1)
    ig.log('STORE_CLERK_REVIEW ' + json.dumps(report, ensure_ascii=False))


def main():
    args = parse_args()
    out = os.path.abspath(args.out or os.path.join(HERE, "..", "..", "Content", "SourceArt", "Blender"))
    name = "SK_NarinClerk"
    folder = os.path.join(out, name)
    bpy.ops.wm.open_mainfile(filepath=os.path.join(folder, name + ".blend"))
    mesh = bpy.data.objects[name]
    for obj in list(bpy.data.objects):
        if obj.type == 'ARMATURE':
            bpy.data.objects.remove(obj, do_unlink=True)
    for modifier in list(mesh.modifiers):
        if modifier.type == 'ARMATURE':
            mesh.modifiers.remove(modifier)
    mesh.parent = None
    for action in list(bpy.data.actions):
        bpy.data.actions.remove(action)
    path = os.path.join(folder, 'manifest.json')
    with open(path, encoding='utf-8') as handle:
        manifest = json.load(handle)
    arms = find_arms(mesh)
    members = arm_members(mesh, arms)
    arm = build_armature(name, arms)
    counts = skin(mesh, arm, members, arms)
    ig.log('STORE_CLERK_WEIGHTS ' + json.dumps(counts, ensure_ascii=False))
    author_idle(arm)
    bpy.context.scene.frame_set(0)
    if args.review:
        ig.preview_material_from_bakes(mesh, {role: os.path.join(folder, filename) for role, filename in manifest['textures'].items()})
        review(mesh, arm, os.path.abspath(args.review))
        return
    ig.ensure_primary_uv(mesh)
    export_skeletal_fbx(os.path.join(folder, name + '.fbx'), arm, mesh)
    manifest['skeletal'] = True
    manifest['animations'] = {'Idle': {'frames': 180, 'fps': 30, 'loop': True}}
    manifest['triangles'] = sum(len(poly.vertices) - 2 for poly in mesh.data.polygons)
    note = (' 몸통·머리와 좌우 위팔·아래팔·손 리그, 손가락 끝 자리만 알려 주는 손끝 뼈(2026-10-19). 몸은 팔을 22도 벌린 그림으로 다시 뽑아 팔이'
            ' 몸에서 떨어져 있다. 기본 자세가 팔을 벌린 자세라 서 있을 때 내린 팔은 게임이 만든다. 6초 호흡 동작.')
    manifest['notes'] = manifest.get('notes', '').split(' 몸통·머리')[0] + note
    with open(path, 'w', encoding='utf-8') as handle:
        json.dump(manifest, handle, ensure_ascii=False, indent=2)
        handle.write('\n')
    ig.preview_material_from_bakes(mesh, {role: os.path.join(folder, filename) for role, filename in manifest['textures'].items()})
    ig.render_preview(mesh, os.path.join(folder, name + '_front.png'), camera_yaw_deg=180, camera_pitch_deg=8)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, name + '.blend'))
    ig.log('STORE_CLERK_RIG PASS bones=%d animation=Idle frames=180' % len(arm.data.bones))


main()
