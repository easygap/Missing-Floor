"""끊어진 스캔 표면을 정점 위치와 법선으로 복원한다. Blender 빌더에서 호출한다."""
import argparse
import numpy as np
import pymeshlab

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source')
parser.add_argument('output')
parser.add_argument('--budget', type=int, required=True)
parser.add_argument('--body', action='store_true')
args = parser.parse_args()
source = np.load(args.source)
points = source['vertices']
normals = source['normals'].copy()
# 생성 원본은 피부 앞면에서도 법선 절반이 안쪽을 향했다. 중심에서 바깥쪽으로
# 방향을 맞춰야 Poisson과 복셀 변환이 얼굴을 안팎으로 찢지 않는다.
center = (points.min(axis=0)+points.max(axis=0))*.5
meshes = pymeshlab.MeshSet()
meshes.add_mesh(pymeshlab.Mesh(vertex_matrix=points, v_normals_matrix=normals))
if args.body:
    # 팔과 다리 사이에서는 전신 중심을 기준으로 뒤집으면 안쪽 면이 사라진다.
    # 원본 자세의 뼈대에 가까운 축을 골라 각 팔다리 바깥으로 법선을 맞춘다.
    meshes.compute_normal_for_point_clouds(k=24, smoothiter=2)
    normals=meshes.current_mesh().vertex_normal_matrix()
    # 팔은 22도 벌린 몸(2026-10-19)의 어깨·팔꿈치·손목·손끝을 잇는다. 예전처럼 팔을 내린 자리로 두면
    # 벌린 팔 둘레의 법선이 몸 쪽으로 뒤집혀 Poisson이 팔과 조끼 사이를 막으로 메웠다.
    segments=[((0,0,.82),(0,0,1.32))]
    for side in (-1,1):
        segments += [((0,0,1.29),(side*.19,-.02,1.28)),
                     ((side*.19,-.02,1.28),(side*.317,.014,1.054)),
                     ((side*.317,.014,1.054),(side*.369,.072,.836)),
                     ((side*.369,.072,.836),(side*.404,.11,.69)),
                     ((side*.085,0,.84),(side*.11,0,.08)),
                     ((side*.11,0,.055),(side*.11,.10,.055))]
    distance=np.full(len(points),np.inf); radial=np.zeros_like(points)
    for begin,end in segments:
        a=np.array(begin); direction=np.array(end)-a
        t=np.clip(((points-a)*direction).sum(1)/(direction*direction).sum(),0,1)
        delta=points-(a+t[:,None]*direction)
        squared=(delta*delta).sum(1); closer=squared<distance
        radial[closer]=delta[closer];distance[closer]=squared[closer]
else:
    radial=points-center
    radial[:,2]*=.4
normals[(normals*radial).sum(axis=1)<0]*=-1
meshes=pymeshlab.MeshSet()
meshes.add_mesh(pymeshlab.Mesh(vertex_matrix=points,v_normals_matrix=normals))
meshes.generate_surface_reconstruction_screened_poisson(depth=9, pointweight=4, samplespernode=2,
                                                        threads=4)
meshes.meshing_remove_duplicate_vertices()
meshes.meshing_remove_unreferenced_vertices()
meshes.meshing_remove_connected_component_by_face_number(mincomponentsize=100)
# 밀도 축소는 Blender에서 작은 틈을 닫은 뒤 한다. 여기서 먼저 줄이면
# 수백 개의 미세한 구멍을 남기려다 옷 주름이 커다란 삼각형으로 당겨진다.
topology=meshes.get_topological_measures()
if not topology['is_mesh_two_manifold']:
    raise RuntimeError(f'복원 표면에 겹친 면이 남았다: {topology}')
if not args.body:
    # 머리는 복셀 변환 시 얇은 눈꺼풀과 머리카락이 지워져 직접 줄인다.
    meshes.meshing_decimation_quadric_edge_collapse(targetfacenum=args.budget,preservenormal=True,
                                                   preservetopology=False,qualitythr=.5)
mesh = meshes.current_mesh()
np.savez(args.output, vertices=mesh.vertex_matrix(), faces=mesh.face_matrix())
print(f'SURFACE_REPAIR vertices={mesh.vertex_number()} triangles={mesh.face_number()}')
print(f'SURFACE_TOPOLOGY {topology}')
