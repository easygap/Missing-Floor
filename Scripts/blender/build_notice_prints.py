"""관리실이 붙인 A4 공지문. 앞면만 인쇄하고 위쪽 두 모서리에 테이프를 붙인다.

SM_NoticeLightsOutA4: 1층 엘리베이터 옆 벽의 「복도 소등 안내」. 인쇄면은
Content/SourceArt/AI/NoticeLightsOut_20261007.png이고, 게임의 읽기 화면 문구와 글자가 같다.
종이는 0.15 mm라 충돌을 넣지 않는다. 읽기는 씬이 같은 크기의 상자로 받는다.

    blender -b --factory-startup --python Scripts/blender/build_notice_prints.py -- <out_dir>
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ig_blender_lib as ig  # noqa: E402


def lights_out_notice():
    ig.reset_scene()
    source = os.path.join(os.path.dirname(__file__), '../../Content/SourceArt/AI/NoticeLightsOut_20261007.png')
    ink = ig.mat_image_uv('LightsOutPrint', source, roughness=.86)
    blank = ig.mat_plastic('PaperBack', (.79, .80, .77), roughness=.9, bump=0)
    paper = ig.image_quad('paper', (.21, .297), (0, 0, 0), ink, thickness=.00015)
    paper.data.materials.append(blank)
    for face in paper.data.polygons:
        if face.normal.y > -.5:
            face.material_index = 1
    # 테이프는 위쪽 두 모서리에만 붙인다. 공동현관 옆 임대 안내문과 같은 손이다.
    tape = ig.mat_plastic('Tape', (.70, .71, .66), roughness=.28, bump=0)
    parts = [paper]
    for x in (-.080, .080):
        parts.append(ig.box('tape', (.018, .00005, .009), (x, -.00011, .135), material=tape))
    # 글자가 읽혀야 하는 종이라 굽기는 1024로 한다. 512로는 본문 획이 뭉개진다.
    ig.build_asset('SM_NoticeLightsOutA4', 'prop', parts, ig.out_root_from_argv(),
                   collision_parts=[], texture_size=1024, mirror_print_for_ue=True, origin="center",
                   notes='A4 21×29.7cm, 종이 0.15mm. 앞 -Y, 원점 중앙. 관리실 공지 「복도 소등 안내」. '
                         '앞면만 인쇄, 위쪽 테이프 2개, 충돌 없음(읽기는 씬의 상자).')


lights_out_notice()
