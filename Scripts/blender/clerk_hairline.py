"""나린 머리선 위로 드러난 살색을 머리색으로 덮는다.

생성한 머리는 가르마 둘레의 정수리가 살색으로 패여 있고, 그 둘레에 검은 가닥 조각이 들쭉날쭉 남아 계산대에서 보면
머리가 듬성듬성 빠진 것처럼 보였다. 원화(NarinHead_20260914.png)는 가운데 가르마에 머리가 이마 위를 덮는다.
머리 UV 삼각형을 텍스처 칸에 그려 칸마다 몸 위 자리를 구하고, 코끝 높이를 기준으로 잡은 머리선보다 위에 있는
살색 칸만 지금 머리 칸의 색으로 바꾼다. 머리선 아래의 앞머리 가닥과 귀·목은 그대로 둔다.

blender -b --factory-startup --python clerk_hairline.py -- [--preview-only]
"""
import math
import os
import sys

import bmesh
import bpy
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ig_blender_lib as ig

ROOT = os.path.abspath(os.path.join(HERE, '../..'))
NAME = 'SK_NarinClerk'
FOLDER = os.path.join(ROOT, 'Content/SourceArt/Blender', NAME)
# 머리 UV는 가로 0.34부터다(rebuild_clerk_head.py). 목 이음매(1.34 m) 아래는 몸이다.
HEAD_U = 0.335
NECK_Z = 1.34
# 코끝에서 머리선 가운데까지(m). 눈은 코끝보다 3.5 cm, 눈썹은 그보다 2.2 cm, 이마는 그 위로 5 cm 남짓이다.
HAIRLINE_ABOVE_NOSE = 0.112
# 관자놀이 쪽으로 갈수록 머리선이 내려온다(옆 90도에서 이만큼).
TEMPLE_DROP = 0.030
FEATHER = 0.006


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def position_map(ob, width, height):
    """머리 UV 칸마다 월드 자리와 칠한 칸 표시."""
    mesh = ob.data
    mesh.calc_loop_triangles()
    uvs = mesh.uv_layers.active.data
    world = ob.matrix_world
    verts = np.array([(world @ v.co)[:] for v in mesh.vertices], dtype=np.float64)
    positions = np.zeros((height, width, 3), dtype=np.float32)
    filled = np.zeros((height, width), dtype=bool)
    for tri in mesh.loop_triangles:
        uv = np.array([uvs[i].uv[:] for i in tri.loops], dtype=np.float64)
        if uv[:, 0].min() < HEAD_U:
            continue
        p = verts[list(tri.vertices)]
        px = uv[:, 0] * width - 0.5
        py = uv[:, 1] * height - 0.5
        x0, x1 = int(math.floor(px.min())), int(math.ceil(px.max()))
        y0, y1 = int(math.floor(py.min())), int(math.ceil(py.max()))
        x0, y0 = max(x0, 0), max(y0, 0)
        x1, y1 = min(x1, width - 1), min(y1, height - 1)
        if x1 < x0 or y1 < y0:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
        d = (py[1] - py[2]) * (px[0] - px[2]) + (px[2] - px[1]) * (py[0] - py[2])
        if abs(d) < 1e-12:
            continue
        a = ((py[1] - py[2]) * (gx - px[2]) + (px[2] - px[1]) * (gy - py[2])) / d
        b = ((py[2] - py[0]) * (gx - px[2]) + (px[0] - px[2]) * (gy - py[2])) / d
        c = 1.0 - a - b
        inside = (a >= -0.02) & (b >= -0.02) & (c >= -0.02)
        if not inside.any():
            continue
        point = a[..., None] * p[0] + b[..., None] * p[1] + c[..., None] * p[2]
        positions[gy[inside], gx[inside]] = point[inside]
        filled[gy[inside], gx[inside]] = True
    return positions, filled


def hair_mask(positions, filled, head_vertices):
    """머리선보다 위인 칸(0~1). 앞은 +Y다(생성 메시가 그렇게 서 있다)."""
    head = head_vertices[head_vertices[:, 2] > NECK_Z + 0.04]
    center_xy = head[:, :2].mean(axis=0)
    face_band = head[(head[:, 2] > NECK_Z + 0.07) & (head[:, 2] < NECK_Z + 0.20)]
    nose = face_band[np.argmax(face_band[:, 1])]
    hairline = nose[2] + HAIRLINE_ABOVE_NOSE
    offset = positions[..., :2] - center_xy
    # 앞(+Y)에서 잰 각. 0이면 정면, 1이면 옆(90도)이고 뒤로 갈수록 커진다.
    turn = np.abs(np.degrees(np.arctan2(offset[..., 0], offset[..., 1]))) / 90.0
    line = hairline - TEMPLE_DROP * np.clip(turn, 0.0, 1.0)
    above = smoothstep(line - FEATHER, line + FEATHER, positions[..., 2])
    # 귀 뒤(옆에서 더 돌아간 곳)는 목덜미까지 머리다. 귀(옆 90~105도)는 그대로 둔다.
    behind = smoothstep(1.2, 1.4, turn) * smoothstep(NECK_Z + 0.05, NECK_Z + 0.08, positions[..., 2])
    mask = np.maximum(above, behind) * filled
    return mask, hairline, nose


def apply_hairline(ob, path):
    """ob(머리 UV가 0.34부터인 나린 메시)의 색 텍스처 path에서 머리선 위 살색 칸을 머리색으로 바꿔 저장한다.
    rebuild_clerk_head.py도 머리를 다시 구운 뒤 부른다. 이미 바꾼 텍스처에 다시 불러도 어두운 칸은 그대로다."""
    image = bpy.data.images.load(path, check_existing=False)
    image.colorspace_settings.name = 'sRGB'
    pixels = ig._pixels(image).copy()
    bpy.data.images.remove(image)
    height, width = pixels.shape[:2]
    positions, filled = position_map(ob, width, height)
    verts = np.array([(ob.matrix_world @ v.co)[:] for v in ob.data.vertices])
    mask, hairline, nose = hair_mask(positions, filled, verts)
    rgb = pixels[..., :3]
    lum = rgb.mean(axis=2)
    # 지금 머리 칸의 색(어두운 칸 가운데 값)으로 칠하고, 원래 밝기 결은 조금 남긴다.
    dark = filled & (lum < 0.12) & (mask > 0.5)
    hair = np.median(rgb[dark], axis=0) if dark.sum() > 500 else np.array([0.035, 0.030, 0.028])
    # 이미 검은 가닥은 그대로 두고 밝은 칸(살색·회색)만 덮는다.
    light = smoothstep(0.09, 0.18, lum)
    weight = (mask * light)[..., None]
    grain = 0.85 + 0.3 * (lum[..., None] - lum[filled].mean())
    rgb[:] = rgb * (1.0 - weight) + hair * np.clip(grain, 0.7, 1.15) * weight
    changed = int((weight[..., 0] > 0.5).sum())
    result = ig._new_image(NAME + '_D_hair', width, srgb=True)
    result.pixels.foreach_set(pixels.reshape(-1))
    ig._save(result, path)
    bpy.data.images.remove(result)
    ig.log(f'CLERK_HAIRLINE nose_z={nose[2]:.3f} hairline_z={hairline:.3f} hair_rgb={hair.round(3).tolist()} '
           f'changed_texels={changed}')


def main():
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    preview_only = '--preview-only' in args
    bpy.ops.wm.open_mainfile(filepath=os.path.join(FOLDER, NAME + '.blend'))
    ob = bpy.data.objects[NAME]
    if not preview_only:
        apply_hairline(ob, os.path.join(FOLDER, NAME + '_D.png'))
    # 확대 검수용 그림(내보내지 않는다).
    with open(os.path.join(FOLDER, 'manifest.json'), encoding='utf-8') as handle:
        import json
        manifest = json.load(handle)
    textures = {role: os.path.join(FOLDER, filename) for role, filename in manifest['textures'].items()}
    ig.preview_material_from_bakes(ob, textures)
    # 목 아래를 잘라 낸 머리 사본만 찍는다(rebuild_clerk_head.py와 같은 구도).
    close = ob.copy()
    close.data = ob.data.copy()
    close.parent = None
    close.matrix_world = ob.matrix_world.copy()
    for modifier in list(close.modifiers):
        close.modifiers.remove(modifier)
    bpy.context.collection.objects.link(close)
    bm = bmesh.new()
    bm.from_mesh(close.data)
    bmesh.ops.bisect_plane(bm, geom=list(bm.verts) + list(bm.edges) + list(bm.faces), dist=0.00001,
                           plane_co=(0, 0, NECK_Z), plane_no=(0, 0, 1), clear_inner=True)
    bm.to_mesh(close.data)
    bm.free()
    ob.hide_render = True
    for armature in [o for o in bpy.data.objects if o.type == 'ARMATURE']:
        armature.hide_render = True
    review = os.path.join(ROOT, 'Saved', 'HeadReview')
    os.makedirs(review, exist_ok=True)
    for angle in (0, 35, 70):
        ig.render_preview(close, os.path.join(review, f'{NAME}_hair_{angle}.png'), camera_yaw_deg=180 + angle,
                          camera_pitch_deg=3, distance_scale=1.3)
    ig.log('CLERK_HAIRLINE PASS')


if __name__ == '__main__':
    main()
