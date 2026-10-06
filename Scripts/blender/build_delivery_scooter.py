"""골목 배달 오토바이. SM_DeliveryScooter(차체), SM_DeliveryScooterWheel(바퀴), SM_DeliveryRider(라이더).

한국 골목의 배달 스쿠터는 125cc 스텝스루 차체에 짐받이를 달고 빨간 배달통을
얹는다. 축간거리 128 cm, 바퀴 지름 54 cm, 핸들 폭 72 cm로 잡았다.
IGNeighborhoodLifeDirector가 이 치수를 그대로 쓴다(바퀴 중심 X ±64, Z 27).

- 차체: 원점은 두 바퀴 사이 바닥 중심, 앞이 +X. 바퀴는 넣지 않는다.
- 바퀴: 원점은 바퀴 중심, 축은 Y. 게임이 앞뒤 두 자리에 같은 메시를 달고 굴린다.
- 라이더: 차체와 같은 원점에 앉아 핸들을 잡은 자세. 세워 둔 오토바이에서는 숨긴다.

충돌은 없다. 서 있는 오토바이를 막는 상자는 게임 코드가 따로 둔다.

    blender -b --factory-startup --python Scripts/blender/build_delivery_scooter.py -- <out_dir> [scooter|wheel|rider ...]
"""

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import ig_blender_lib as ig  # noqa: E402

WHEEL_X = 0.64
WHEEL_RADIUS = 0.27


def build_scooter(out_root):
    ig.reset_scene()
    paint = ig.mat_painted_steel("ScooterPaint", (0.11, 0.13, 0.17), roughness=0.32, wear=0.12, bump=0.01)
    trim = ig.mat_plastic("ScooterTrim", (0.03, 0.03, 0.035), roughness=0.55, bump=0.01)
    seat = ig.mat_plastic("ScooterSeat", (0.02, 0.02, 0.022), roughness=0.72, bump=0.02, grain_scale=400.0)
    metal = ig.mat_metal("ScooterMetal", (0.50, 0.51, 0.53), roughness=0.38, streak=0.08)
    lens = ig.mat_gloss("ScooterHeadlampLens", (0.78, 0.78, 0.74), roughness=0.08)
    tail = ig.mat_gloss("ScooterTailLens", (0.45, 0.02, 0.02), roughness=0.10)
    box_red = ig.mat_plastic("DeliveryBoxRed", (0.55, 0.05, 0.04), roughness=0.45, bump=0.01)
    reflect = ig.mat_plastic("ReflectStrip", (0.78, 0.78, 0.74), roughness=0.25, bump=0.0)
    plate = ig.mat_plastic("PlateBlank", (0.82, 0.82, 0.78), roughness=0.35, bump=0.0)
    parts = []

    # 발판. 앞바퀴와 뒷바퀴 사이의 낮은 바닥이다.
    parts.append(ig.box("floorboard", (0.58, 0.32, 0.06), location=(0.02, 0.0, 0.30), bevel=0.015,
                        segments=2, material=trim))
    # 다리 가리개(안쪽 판)와 앞 카울. 카울 아래끝은 앞 타이어 위(Z 0.54)보다 높다.
    parts.append(ig.box("leg_shield", (0.06, 0.44, 0.66), location=(0.33, 0.0, 0.64), rotation=(0.0, 0.09, 0.0),
                        bevel=0.02, segments=2, material=paint))
    parts.append(ig.box("front_cowl", (0.24, 0.40, 0.48), location=(0.47, 0.0, 0.83), bevel=0.05, segments=3,
                        material=paint))
    parts.append(ig.box("headlamp_housing", (0.06, 0.28, 0.11), location=(0.585, 0.0, 0.93), bevel=0.02,
                        segments=2, material=trim))
    parts.append(ig.box("headlamp_lens", (0.012, 0.24, 0.08), location=(0.617, 0.0, 0.93), bevel=0.004,
                        segments=1, material=lens))
    # 앞 포크 둘과 흙받이.
    for side in (-1.0, 1.0):
        parts.append(ig.pipe(f"fork_{'l' if side < 0 else 'r'}",
                             [(WHEEL_X, side * 0.085, WHEEL_RADIUS), (0.53, side * 0.085, 0.66)],
                             0.022, resolution=10, material=metal))
    parts.append(ig.box("front_fender", (0.36, 0.13, 0.035), location=(0.66, 0.0, 0.575),
                        rotation=(0.0, -0.08, 0.0), bevel=0.01, segments=1, material=trim))
    # 핸들. 커버, 봉, 손잡이 둘, 거울 둘.
    parts.append(ig.box("handlebar_cover", (0.13, 0.30, 0.08), location=(0.38, 0.0, 1.03), bevel=0.025,
                        segments=2, material=paint))
    parts.append(ig.pipe("handlebar", [(0.36, -0.36, 1.04), (0.36, 0.36, 1.04)], 0.016, resolution=10,
                         material=metal))
    for side in (-1.0, 1.0):
        tag = "l" if side < 0 else "r"
        parts.append(ig.cylinder(f"grip_{tag}", 0.021, 0.12, location=(0.36, side * 0.31, 1.04),
                                 rotation=(math.pi * 0.5, 0.0, 0.0), segments=14, material=seat))
        parts.append(ig.pipe(f"mirror_stalk_{tag}", [(0.37, side * 0.22, 1.07), (0.34, side * 0.29, 1.29)],
                             0.008, resolution=8, material=metal))
        parts.append(ig.box(f"mirror_{tag}", (0.025, 0.12, 0.075), location=(0.335, side * 0.30, 1.32),
                            bevel=0.01, segments=1, material=trim))
    # 시트와 뒤 차체. 시트 윗면은 Z 0.83, 라이더의 엉덩이가 여기 앉는다.
    parts.append(ig.box("seat", (0.66, 0.30, 0.10), location=(-0.26, 0.0, 0.78), bevel=0.04, segments=3,
                        material=seat))
    parts.append(ig.box("rear_body", (0.86, 0.36, 0.30), location=(-0.28, 0.0, 0.58), bevel=0.06, segments=3,
                        material=paint))
    parts.append(ig.box("side_trim", (0.70, 0.37, 0.03), location=(-0.30, 0.0, 0.46), bevel=0.01, segments=1,
                        material=trim))
    # 엔진과 스윙암(왼쪽), 머플러(오른쪽).
    parts.append(ig.box("engine_unit", (0.50, 0.14, 0.18), location=(-0.40, -0.10, 0.31), bevel=0.03,
                        segments=2, material=metal))
    parts.append(ig.pipe("muffler", [(-0.10, 0.17, 0.25), (-0.38, 0.20, 0.35), (-0.74, 0.20, 0.42)], 0.045,
                         resolution=12, corner_radius=0.10, material=trim))
    # 뒤 흙받이, 꼬리등, 번호판(글자 없음).
    parts.append(ig.box("rear_fender", (0.24, 0.20, 0.05), location=(-0.80, 0.0, 0.62), rotation=(0.0, 0.30, 0.0),
                        bevel=0.012, segments=1, material=trim))
    parts.append(ig.box("tail_lens", (0.03, 0.18, 0.05), location=(-0.73, 0.0, 0.71), bevel=0.008, segments=1,
                        material=tail))
    parts.append(ig.box("plate", (0.005, 0.17, 0.09), location=(-0.86, 0.0, 0.53), rotation=(0.0, 0.25, 0.0),
                        material=plate))
    # 짐받이와 배달통. 배달통 아래면 Z 0.88, 윗면 Z 1.28.
    rack = [(-0.42, -0.16, 0.86), (-0.78, -0.16, 0.86), (-0.78, 0.16, 0.86), (-0.42, 0.16, 0.86),
            (-0.42, -0.16, 0.86)]
    parts.append(ig.pipe("rack", rack, 0.012, resolution=8, corner_radius=0.03, material=trim))
    parts.append(ig.box("delivery_box", (0.42, 0.40, 0.40), location=(-0.60, 0.0, 1.08), bevel=0.02, segments=2,
                        material=box_red))
    parts.append(ig.box("delivery_box_lid_seam", (0.425, 0.405, 0.012), location=(-0.60, 0.0, 1.23),
                        material=trim))
    parts.append(ig.box("delivery_box_strip", (0.425, 0.405, 0.035), location=(-0.60, 0.0, 1.00),
                        material=reflect))
    parts.append(ig.box("delivery_box_latch", (0.02, 0.08, 0.05), location=(-0.815, 0.0, 1.20), material=metal))

    return ig.build_asset(
        "SM_DeliveryScooter", "prop", parts, out_root, collision_parts=[],
        notes=("골목 배달 스쿠터 차체 190 x 72 x 128. 원점은 두 바퀴 사이 바닥 중심, 앞 +X. "
               "바퀴는 SM_DeliveryScooterWheel을 X ±64, Z 27에 단다. 충돌 없음(게임이 막힘 상자를 둔다)."),
        texture_size=1024, preview_yaw=55.0)


def build_wheel(out_root):
    ig.reset_scene()
    rubber = ig.mat_rubber("ScooterTire", (0.025, 0.025, 0.025), roughness=0.88)
    rim = ig.mat_metal("ScooterRim", (0.30, 0.31, 0.33), roughness=0.42, streak=0.05)
    parts = []
    # 축이 Y인 바퀴. 라뜨는 Z축으로 돌므로 X로 90도 눕힌다.
    parts.append(ig.torus("tire", WHEEL_RADIUS - 0.065, 0.065, rotation=(math.pi * 0.5, 0.0, 0.0),
                          major_segments=40, minor_segments=12, material=rubber))
    parts.append(ig.cylinder("rim", 0.175, 0.085, rotation=(math.pi * 0.5, 0.0, 0.0), segments=32, material=rim))
    parts.append(ig.cylinder("hub", 0.055, 0.13, rotation=(math.pi * 0.5, 0.0, 0.0), segments=20, material=rim))
    # 바큇살 다섯. 굴러갈 때 도는 게 보인다.
    for index in range(5):
        angle = math.tau * index / 5.0
        parts.append(ig.box(f"spoke_{index}", (0.12, 0.03, 0.025),
                            location=(math.cos(angle) * 0.11, 0.0, math.sin(angle) * 0.11),
                            rotation=(0.0, -angle, 0.0), material=rim))
    return ig.build_asset(
        "SM_DeliveryScooterWheel", "prop", parts, out_root, collision_parts=[],
        notes="배달 스쿠터 바퀴 지름 54, 폭 13. 원점은 바퀴 중심, 축 Y. 충돌 없음.",
        texture_size=512, origin="center", preview_yaw=90.0)


def build_rider(out_root):
    ig.reset_scene()
    jacket = ig.mat_plastic("RiderJacket", (0.05, 0.06, 0.09), roughness=0.78, bump=0.03, grain_scale=150.0)
    pants = ig.mat_plastic("RiderPants", (0.03, 0.03, 0.035), roughness=0.82, bump=0.02)
    helmet = ig.mat_gloss("RiderHelmet", (0.02, 0.02, 0.025), roughness=0.22)
    visor = ig.mat_gloss("RiderVisor", (0.01, 0.01, 0.012), roughness=0.05)
    glove = ig.mat_rubber("RiderGlove", (0.02, 0.02, 0.02), roughness=0.75)
    reflect = ig.mat_plastic("RiderReflect", (0.72, 0.72, 0.70), roughness=0.3, bump=0.0)
    parts = []

    # 시트 윗면(Z 0.83)에 앉는다. 상체는 핸들 쪽으로 17도 숙였다.
    parts.append(ig.box("pelvis", (0.26, 0.34, 0.18), location=(-0.22, 0.0, 0.92), bevel=0.06, segments=2,
                        material=pants))
    torso = ig.box("torso", (0.26, 0.40, 0.52), location=(-0.11, 0.0, 1.20), rotation=(0.0, 0.30, 0.0),
                   bevel=0.08, segments=3, material=jacket)
    parts.append(torso)
    parts.append(ig.box("reflect_band", (0.265, 0.405, 0.035), location=(-0.09, 0.0, 1.16),
                        rotation=(0.0, 0.30, 0.0), material=reflect))
    parts.append(ig.cylinder("neck", 0.05, 0.10, location=(0.01, 0.0, 1.47), segments=14, material=jacket))
    # 헬멧은 둥근 셸에 어두운 바이저. 얼굴은 바이저 뒤에 없다.
    parts.append(ig.lathe("helmet", [(0.0, -0.11), (0.10, -0.115), (0.145, -0.05), (0.152, 0.02), (0.13, 0.09),
                                     (0.08, 0.135), (0.0, 0.15)], segments=28, location=(0.03, 0.0, 1.60),
                          material=helmet))
    parts.append(ig.box("visor", (0.04, 0.22, 0.10), location=(0.165, 0.0, 1.60), rotation=(0.0, -0.15, 0.0),
                        bevel=0.015, segments=1, material=visor))
    for side in (-1.0, 1.0):
        tag = "l" if side < 0 else "r"
        shoulder = (0.0, side * 0.20, 1.38)
        elbow = (0.17, side * 0.27, 1.17)
        hand = (0.355, side * 0.31, 1.05)
        parts.append(ig.pipe(f"upper_arm_{tag}", [shoulder, elbow], 0.058, resolution=10, material=jacket))
        parts.append(ig.pipe(f"forearm_{tag}", [elbow, hand], 0.047, resolution=10, material=jacket))
        parts.append(ig.box(f"glove_{tag}", (0.10, 0.07, 0.08), location=hand, bevel=0.02, segments=1,
                            material=glove))
        hip = (-0.16, side * 0.11, 0.90)
        knee = (0.17, side * 0.17, 0.82)
        ankle = (0.23, side * 0.15, 0.42)
        parts.append(ig.pipe(f"thigh_{tag}", [hip, knee], 0.078, resolution=10, material=pants))
        parts.append(ig.pipe(f"shin_{tag}", [knee, ankle], 0.060, resolution=10, material=pants))
        # 신발 바닥이 발판 윗면(Z 0.33)에 닿는다.
        parts.append(ig.box(f"shoe_{tag}", (0.27, 0.10, 0.09), location=(0.28, side * 0.15, 0.375), bevel=0.02,
                            segments=1, material=glove))
    return ig.build_asset(
        "SM_DeliveryRider", "prop", parts, out_root, collision_parts=[],
        notes=("배달 라이더. 스쿠터와 같은 원점(바닥 중심, 앞 +X)에 앉은 자세, 키 172. 얼굴은 어두운 바이저 뒤다. "
               "세워 둔 오토바이에서는 게임이 숨긴다. 충돌 없음."),
        texture_size=1024, preview_yaw=55.0)


BUILDERS = {"scooter": build_scooter, "wheel": build_wheel, "rider": build_rider}


def main():
    out_root = ig.out_root_from_argv()
    only = [a for a in sys.argv[sys.argv.index("--") + 2:]] if "--" in sys.argv else []
    for key, builder in BUILDERS.items():
        if not only or key in only:
            builder(out_root)


main()
