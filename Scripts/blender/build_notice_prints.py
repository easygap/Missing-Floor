"""관리실이 붙인 A4 공지문. 앞면만 인쇄하고 위쪽 두 모서리에 테이프를 붙인다.

SM_NoticeLightsOutA4: 1층 엘리베이터 옆 벽의 「복도 소등 안내」. 인쇄면은
Content/SourceArt/AI/NoticeLightsOut_20261007.png이고, 게임의 읽기 화면 문구와 글자가 같다.
SM_NoticeEvictionA4: 셋째 낮에 관리인이 4층 엘리베이터 옆에 붙이는 「퇴거 요구서」. 인쇄면은
Content/SourceArt/AI/NoticeEviction_20261103.png이고, 설정집 §8 3-9의 종이 문구(사유와 기한)와 같다.
SM_NoticeRoofRepairA4: 그때 엘리베이터 쪽 옆에 같이 붙는 「옥상 누수 긴급 보수」 안내. 인쇄면은
Content/SourceArt/AI/NoticeRoofRepair_20261103.png이고, 사진 문자가 말하는 내일 오전 7시가 여기 적혀 있다.
종이는 0.15 mm라 충돌을 넣지 않는다. 소등 안내는 씬이, 요구서는 증거 액터가 같은 크기의 상자로 읽기를 받는다.

    blender -b --factory-startup --python Scripts/blender/build_notice_prints.py -- <out_dir>
    (IG_ONLY=SM_NoticeEvictionA4 로 한 장만)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ig_blender_lib as ig  # noqa: E402

SOURCE_ART = os.path.join(os.path.dirname(__file__), '../../Content/SourceArt/AI')


def taped_a4(asset_name, ink, notes):
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
    ig.build_asset(asset_name, 'prop', parts, ig.out_root_from_argv(),
                   collision_parts=[], texture_size=1024, mirror_print_for_ue=True, origin="center",
                   notes=notes)


def lights_out_notice():
    ig.reset_scene()
    ink = ig.mat_image_uv('LightsOutPrint', os.path.join(SOURCE_ART, 'NoticeLightsOut_20261007.png'), roughness=.86)
    taped_a4('SM_NoticeLightsOutA4', ink,
             'A4 21×29.7cm, 종이 0.15mm. 앞 -Y, 원점 중앙. 관리실 공지 「복도 소등 안내」. '
             '앞면만 인쇄, 위쪽 테이프 2개, 충돌 없음(읽기는 씬의 상자).')


def srgb_to_linear(value):
    value /= 255.0
    return value / 12.92 if value <= .04045 else ((value + .055) / 1.055) ** 2.4


def fresh_print(name, file_name):
    """셋째 낮 아침에 막 뽑은 인쇄면. 원본 바탕이 순백(255)이라 그대로 구우면 복도 벽 옆에서 종이만
    떠 보인다. 막 뽑은 복사용지 밝기를 곱한다. 몇 달 붙어 누레진 소등 안내(242, 238, 227)보다 덜 누렇다."""
    ink = ig.mat_image_uv(name, os.path.join(SOURCE_ART, file_name), roughness=.86)
    nodes, links = ink.node_tree.nodes, ink.node_tree.links
    image = next(node for node in nodes if node.type == 'TEX_IMAGE')
    bsdf = next(node for node in nodes if node.type == 'BSDF_PRINCIPLED')
    paper_white = nodes.new('ShaderNodeVectorMath')
    paper_white.operation = 'MULTIPLY'
    paper_white.inputs[1].default_value = tuple(srgb_to_linear(v) for v in (242, 241, 237))
    links.new(image.outputs['Color'], paper_white.inputs[0])
    links.new(paper_white.outputs['Vector'], bsdf.inputs['Base Color'])
    return ink


def eviction_notice():
    ig.reset_scene()
    taped_a4('SM_NoticeEvictionA4', fresh_print('EvictionPrint', 'NoticeEviction_20261103.png'),
             'A4 21×29.7cm, 종이 0.15mm. 앞 -Y, 원점 중앙. 관리인이 붙인 「퇴거 요구서」. '
             '앞면만 인쇄, 위쪽 테이프 2개, 충돌 없음(겨누기는 증거 액터가 메시 경계로 세운다).')


def roof_repair_notice():
    ig.reset_scene()
    taped_a4('SM_NoticeRoofRepairA4', fresh_print('RoofRepairPrint', 'NoticeRoofRepair_20261103.png'),
             'A4 21×29.7cm, 종이 0.15mm. 앞 -Y, 원점 중앙. 관리실 공지 「옥상 누수 긴급 보수」. '
             '앞면만 인쇄, 위쪽 테이프 2개, 충돌 없음(읽기 없이 붙어만 있다).')


only = set(filter(None, os.environ.get('IG_ONLY', '').split(',')))
for name, build in (('SM_NoticeLightsOutA4', lights_out_notice), ('SM_NoticeEvictionA4', eviction_notice),
                    ('SM_NoticeRoofRepairA4', roof_repair_notice)):
    if not only or name in only:
        build()
