"""얼굴 확대 원화의 입체 머리를 전신 원본에 연결한다. 정면 사진 투영은 쓰지 않는다."""
import os
import sys
import json
import subprocess
import shutil
import numpy as np
import bpy
import bmesh
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ig_blender_lib as ig
from clerk_surface_finish import connect_neck, match_neck_material

ROOT = os.path.abspath(os.path.join(HERE, '../..'))
NAME = 'SK_NarinClerk'
OUT = os.path.join(ROOT, 'Content/SourceArt/Blender', NAME)


def load_source(relative, height, bottom):
    before = set(bpy.context.scene.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(ROOT, relative))
    parts = [o for o in bpy.context.scene.objects if o not in before and o.type == 'MESH']
    ob = ig.join(parts, 'source')
    lo, hi = ig.bounds(ob)
    center = (lo + hi) * .5
    scale = height / (hi.z - lo.z)
    for v in ob.data.vertices:
        v.co = Vector(((v.co.x-center.x)*scale, (v.co.y-center.y)*scale,
                       (v.co.z-lo.z)*scale+bottom))
    ig.mirror_y(ob)
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=.00005)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    center = Vector((0,0,bottom+height*.5))
    for face in bm.faces:
        radial = face.calc_center_median()-center
        radial.z *= .4
        if face.normal.dot(radial)<0:
            face.normal_flip()
    bm.to_mesh(ob.data)
    bm.free()
    ig.prepare_organic_source(ob, roughness_floor=.56)
    if height < .5:
        # 원화의 검은 머리와 맞춘다. 피부의 붉은 기와 얼굴 중앙은 마스크에서 뺀다.
        for material in ob.data.materials:
            bsdf = ig._principled(material)
            nodes, links = material.node_tree.nodes, material.node_tree.links
            color = bsdf.inputs['Base Color'].links[0].from_socket
            rgb = nodes.new('ShaderNodeSeparateColor'); links.new(color,rgb.inputs[0])
            def math_node(operation, a, b):
                node=nodes.new('ShaderNodeMath'); node.operation=operation
                for i,value in enumerate((a,b)):
                    if isinstance(value,(float,int)): node.inputs[i].default_value=value
                    else: links.new(value,node.inputs[i])
                return node.outputs[0]
            neutral=math_node('LESS_THAN',math_node('SUBTRACT',rgb.outputs[0],rgb.outputs[1]),.025)
            geo=nodes.new('ShaderNodeNewGeometry'); xyz=nodes.new('ShaderNodeSeparateXYZ')
            links.new(geo.outputs['Position'],xyz.inputs[0])
            crown=math_node('GREATER_THAN',xyz.outputs['Z'],1.55)
            rear=math_node('LESS_THAN',xyz.outputs['Y'],-.015)
            region=math_node('MAXIMUM',crown,rear)
            mix=nodes.new('ShaderNodeMixRGB'); mix.blend_type='MULTIPLY'
            mix.inputs[2].default_value=(.2,.2,.2,1)
            links.new(math_node('MULTIPLY',region,neutral),mix.inputs[0])
            links.new(color,mix.inputs[1]); links.new(mix.outputs[0],bsdf.inputs['Base Color'])
    return ob


def cut(ob, z, keep_lower):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.bisect_plane(bm, geom=list(bm.verts)+list(bm.edges)+list(bm.faces),
                          dist=.00001, plane_co=(0,0,z), plane_no=(0,0,1),
                          clear_outer=keep_lower, clear_inner=not keep_lower)
    bm.to_mesh(ob.data)
    bm.free()


def low_copy(source, budget, body=False):
    # 얇고 뚫린 생성 메시를 바로 복셀화하면 피부에 구멍이 번진다.
    # 방향 있는 표면점으로 닫힌 껍질을 먼저 복원한 뒤 줄인다.
    folder = os.path.join(ROOT, 'Saved/HeadReview')
    os.makedirs(folder, exist_ok=True)
    points = os.path.join(folder, f'surface-{budget}-points.npz')
    result = os.path.join(folder, f'surface-{budget}-closed.npz')
    source.data.update()
    np.savez(points, vertices=np.array([v.co[:] for v in source.data.vertices]),
             normals=np.array([v.normal[:] for v in source.data.vertices]))
    # PATH에서 먼저 잡히는 python이 이 저장소 것이 아닐 때가 있다(다른 도구의 가상환경). IG_PYTHON으로 정한다.
    command=[os.environ.get('IG_PYTHON') or shutil.which('python'), os.path.join(ROOT, 'Scripts/repair_scanned_surface.py'),
             points, result, '--budget', str(budget)]
    if body: command.append('--body')
    subprocess.run(command,check=True)
    surface = np.load(result)
    data = bpy.data.meshes.new('ClosedSurface')
    data.from_pydata(surface['vertices'].tolist(), [], surface['faces'].tolist())
    ob = bpy.data.objects.new('ClosedSurface',data)
    bpy.context.collection.objects.link(ob)
    if body:
        mod=ob.modifiers.new('옷의 작은 틈 닫기','REMESH')
        mod.mode='VOXEL';mod.voxel_size=.012
        mod.use_smooth_shade=True;ig.apply_modifiers(ob)
        mod=ob.modifiers.new('천 표면 정리','SMOOTH')
        mod.factor=.5;mod.iterations=4
        ig.apply_modifiers(ob)
    ig.remove_small_islands(ob, max_diameter=.018)
    ig.decimate(ob,budget)
    for p in ob.data.polygons:
        p.use_smooth = True
    ob.data.materials.clear()
    ig.uv_smart(ob, margin=.003)
    return ob


# 몸 원본. 2026-10-19부터 팔을 22도 벌린 입력 그림(Scripts/make_narin_arms_out.py)으로 뽑은 몸을 쓴다.
# 팔을 내린 그림으로 뽑은 몸(front-v2)은 팔 안쪽과 손이 조끼·바지에 한 면으로 붙어 팔을 움직일 수 없었다.
BODY_SOURCE = 'Content/SourceArt/Generated/NarinClerk/arms-out-20261019/raw/pbr_00001_.glb'
if '--' in sys.argv and '--body' in sys.argv[sys.argv.index('--'):]:
    BODY_SOURCE = sys.argv[sys.argv.index('--body') + 1]

ig.reset_scene()
body = load_source(BODY_SOURCE, 1.63, 0)
cut(body, 1.36, True)
head = load_source('Content/SourceArt/Generated/NarinHead/face-closeup-20260914/raw/pbr_00001_.glb', .365, 1.265)
cut(head, 1.34, False)
# 목의 중심을 맞추고 겹치는 2cm는 티셔츠 깃 안쪽에 둔다.
def neck_center(ob):
    points = [v.co for v in ob.data.vertices if 1.34 <= v.co.z <= 1.36]
    return (min(p.y for p in points)+max(p.y for p in points))*.5
offset = neck_center(body)-neck_center(head)
for v in head.data.vertices:
    v.co.y += offset
low_body = low_copy(body, 6500, body=True)
low_head = low_copy(head, 9000)
# 머리에 전체 UV 면적의 2/3를 준다. 몸과 머리의 해상도를 따로 배분한다.
for ob, start, span in ((low_body,0.,.32),(low_head,.34,.66)):
    for loop in ob.data.uv_layers['UVMap'].data:
        loop.uv.x = start + loop.uv.x*span
os.makedirs(OUT, exist_ok=True)
temporary=os.path.join(ROOT,'Saved/HeadReview/Bakes')
head.hide_render=True;low_head.hide_render=True
body_maps=ig.bake_from_high(low_body,body,'Body',temporary,size=4096,
                            cage_extrusion=.025,max_ray_distance=.075)
body.hide_render=True;low_body.hide_render=True
head.hide_render=False;low_head.hide_render=False
head_maps=ig.bake_from_high(low_head,head,'Head',temporary,size=4096,
                            cage_extrusion=.006,max_ray_distance=.030)
textures={}
for role in ('D','N','ORM'):
    images=[]
    for paths in (body_maps,head_maps):
        img=bpy.data.images.load(paths[role],check_existing=False)
        img.colorspace_settings.name='sRGB' if role=='D' else 'Non-Color'
        images.append(img)
    pixels=ig._pixels(images[0]); head_pixels=ig._pixels(images[1]); split=int(4096*.34)
    pixels[:,split:,:]=head_pixels[:,split:,:]
    if role=='N':
        # 불규칙한 원본의 천 조각은 색으로 남기고 노멀로 재생하지 않는다.
        pixels[:,:split,:3]=(.5,.5,1.)
        pixels[:,split:,:2]=.5+(pixels[:,split:,:2]-.5)*.18
    result=ig._new_image(NAME+'_'+role,4096,srgb=role=='D')
    result.pixels.foreach_set(pixels.reshape(-1))
    textures[role]=os.path.join(OUT,NAME+'_'+role+'.png')
    ig._save(result,textures[role])
    for img in images+[result]:bpy.data.images.remove(img)
bpy.data.objects.remove(body,do_unlink=True)
bpy.data.objects.remove(head,do_unlink=True)
low_body.hide_render=False;low_head.hide_render=False
low=connect_neck(low_body,low_head)
low.name=NAME
match_neck_material(low,textures,NAME,OUT)
ig.preview_material_from_bakes(low, textures)
ig.render_preview(low, os.path.join(OUT, NAME+'_preview.png'), camera_yaw_deg=180)
ig.render_preview(low, os.path.join(OUT, NAME+'_preview_torch.png'), camera_yaw_deg=180, flashlight=True)
# 확대 검수용 사본은 내보내지 않는다.
close = low.copy()
close.data = low.data.copy()
bpy.context.collection.objects.link(close)
cut(close, 1.34, False)
low.hide_render = True
for angle in (-55,0,35,55,75,100):
    ig.render_preview(close, os.path.join(OUT, f'{NAME}_head_{angle}.png'),
                      camera_yaw_deg=180+angle, camera_pitch_deg=3, distance_scale=1.3)
bpy.data.objects.remove(close, do_unlink=True)
low.hide_render = False
slots = ig.finalize_slots(low)
ig.ensure_primary_uv(low)
ig.export_fbx(os.path.join(OUT,NAME+'.fbx'), [low])
ig.write_manifest(OUT,NAME,'hero',os.path.join(OUT,NAME+'.fbx'),textures,low,slots,
                  notes='163cm 인물. 전신과 얼굴 원본의 교차면을 없애 목을 연결하고 양쪽 볼 색을 기준으로 피부색을 맞췄다. 삼면 참고 원화 NarinContinuity_20260915.png. 4K UV의 2/3를 머리에 배분한다.')
with open(os.path.join(OUT,'manifest.json'),encoding='utf-8') as f:
    manifest = json.load(f)
manifest['generated_from'] = '../Generated/NarinHead/face-closeup-20260914/raw/pbr_00001_.glb'
with open(os.path.join(OUT,'manifest.json'),'w',encoding='utf-8') as f:
    json.dump(manifest,f,ensure_ascii=False,indent=2)
    f.write('\n')
ig.preview_material_from_bakes(low,textures)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT,NAME+'.blend'))
ig.log(f'CLERK_HEAD PASS triangles={ig.triangle_count(low)} neck_offset={offset:.4f}')
