"""실제 매장 사진에 맞춘 자기질 타일과 도장 벽 마감. 치수 단위는 cm."""

import unreal
import os

MARKER = "IG_RetailFinish_20260914"


def floor_albedo(finish):
    """생성 원본을 직접 가져온다. 예전 타일 스캔으로 덮어쓰지 않는다.

    포천석만은 고운 알갱이를 누그러뜨린 파생본(SourceArt/Retail, soften_fine_detail.py --retail)을
    읽는다. 파생본이 없으면 원본으로 돌아가지 않고 멈춘다."""
    name = "LandingPaint" if finish.startswith("landing_") else ("PocheonGranite" if finish == "granite" else "PorcelainStore")
    asset_name = f"T_{name}_20260915_D"
    source = os.path.join(unreal.SystemLibrary.get_project_directory(),
                          "Content", "SourceArt", "Retail" if name == "PocheonGranite" else "AI",
                          f"{name}_20260915.png")
    if not os.path.isfile(source):
        raise RuntimeError(f"바닥 재질 원본이 없다: {source}")
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = "/Game/Prototype/Textures"
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(f"{task.destination_path}/{asset_name}")
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    # 생성 원본은 비정규 크기일 수 있다. 패딩 대신 늘여서 반복 경계를 보존하고
    # 실제 게임에서는 1K까지 사용한다. 밉과 이방성 필터는 엔진이 만든다.
    texture.set_editor_property("power_of_two_mode", unreal.TexturePowerOfTwoSetting.STRETCH_TO_POWER_OF_TWO)
    texture.set_editor_property("max_texture_size", 1024)
    texture.set_editor_property("never_stream", False)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
    unreal.get_editor_subsystem(unreal.EditorAssetSubsystem).save_loaded_asset(texture)
    return texture


# 공용부의 때. gpt-image로 만든 때 사진(SourceArt/AI/Grime*_20260929)을
# build_grime_masks.py가 반복되게 다듬은 것을 월드 좌표로 편다.
# (텍스처, 한 벌의 크기 cm, 얼룩 세기, 얼룩 자리의 거칠기 변화).
# 편의점 매장 마감은 청소가 잦아 넣지 않는다.
GRIME = {
    "landing_wall": ("WallGrime_M", 420.0, 2.6, -.10),
    "landing_dado": ("WallGrime_M", 340.0, 1.8, -.08),
    "landing_ceiling": ("CeilingStain_M", 520.0, 1.2, .0),
    "granite": ("FloorGrime_M", 300.0, 1.3, .16),
}
# 같은 얼룩을 모든 면에 고르게 입히지 않는다(2026-10-19). 물때는 천장과 만나는 벽 위쪽에서
# 흘러내리고, 천장 얼룩은 비가 스미는 남쪽 바깥벽 쪽에만 번진다. 허리 아래 진한 칠은 걸레와
# 신발 끝이 닿는 바닥 쪽이, 바닥은 걸레가 덜 닿는 벽 가장자리가 탁하다. 복도는 층마다
# Y -375..-235이고 층 높이는 300 cm라 월드 좌표로 자리를 정한다. P는 월드 위치, G는 위 표의 세기.
GRIME_PLACE = {
    "landing_wall": "float h=P.z-floor(P.z/300.0)*300.0; return G*(0.18+0.82*smoothstep(130.0,228.0,h));",
    "landing_dado": "float h=P.z-floor(P.z/300.0)*300.0; return G*(0.25+0.75*(1.0-smoothstep(20.0,70.0,h)));",
    "landing_ceiling": "return G*saturate(1.0-(P.y+375.0)/90.0);",
    "granite": "float e=max(saturate(1.0-(P.y+375.0)/35.0),saturate(1.0-(-235.0-P.y)/35.0)); return G*(0.3+0.7*e);",
}


def grime_mask(name):
    asset_name = f"T_{name}"
    source = os.path.join(unreal.SystemLibrary.get_project_directory(),
                          "Content", "SourceArt", "Grime", f"{name}.png")
    if not os.path.isfile(source):
        raise RuntimeError(f"때 마스크 원본이 없다: {source}")
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = "/Game/Prototype/Textures"
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(f"{task.destination_path}/{asset_name}")
    # 회색 128이 곱하기 1이 되도록 선형으로 읽는다. 옅은 물때가 뭉개지지 않게 BC7.
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_BC7)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    texture.set_editor_property("max_texture_size", 1024)
    texture.set_editor_property("never_stream", False)
    unreal.get_editor_subsystem(unreal.EditorAssetSubsystem).save_loaded_asset(texture)
    return texture


def author(material, finish):
    # 재반입은 이 텍스처를 쓰는 재질을 갱신한다. 노드를 만드는 도중 실행하면
    # 막 연결한 그래프가 이전 저장 상태로 돌아갈 수 있으므로 먼저 끝낸다.
    painted = finish.startswith("landing_")
    albedo = floor_albedo(finish) if finish in ("floor", "granite") or painted else None
    grime = grime_mask(GRIME[finish][0]) if finish in GRIME else None
    lib = unreal.MaterialEditingLibrary
    # UE 5.8의 일괄 삭제는 순회 중 원본 배열을 줄여 일부 노드를 남긴다.
    # 목록을 복사해 하나씩 지워야 재실행 때 텍스처 샘플이 쌓이지 않는다.
    for expression in list(lib.get_material_expressions(material)):
        lib.delete_material_expression(material, expression)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("tangent_space_normal", True)

    def node(kind, **properties):
        obj = lib.create_material_expression(material, getattr(unreal, kind))
        for key, value in properties.items():
            obj.set_editor_property(key, value)
        return obj

    def scalar(value):
        return node("MaterialExpressionConstant", r=value)

    def color(value):
        return node("MaterialExpressionConstant3Vector", constant=unreal.LinearColor(*value, 1))

    def link(a, b, slot):
        if lib.connect_material_expressions(a, "", b, slot) is False:
            raise RuntimeError(f"재질 노드 연결 실패: {b.get_name()} / {slot}")

    # 디딤판과 챌판이 같은 석재를 쓴다. 면 방향에 맞춰 좌표를 고르고
    # 색상과 줄눈이 같은 좌표를 쓰게 해 계단 옆면도 늘어나지 않게 한다.
    position = node("MaterialExpressionWorldPosition")
    if finish == "granite" or painted:
        projected = node("MaterialExpressionCustom",
                         output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                         code="float3 n=abs(N); return float3(n.z>.707 ? P.xy : (n.x>n.y ? P.yz : P.xz),0);",
                         description="석재 면 방향 투영 / 600mm")
        pins = []
        for label in ("P", "N"):
            pin = unreal.CustomInput()
            pin.set_editor_property("input_name", label)
            pins.append(pin)
        projected.set_editor_property("inputs", pins)
        link(position, projected, "P")
        link(node("MaterialExpressionVertexNormalWS"), projected, "N")
        position = projected

    base = color((.68, .69, .665) if finish == "wall" else (.61, .59, .54))
    rough = scalar(.42 if finish == "wall" else .31)
    if finish in ("floor", "granite") or painted:
        # 600mm 실물 사진을 기준으로 한 색상 원본이다. 연마면의 검은 광물은
        # 구멍이 아니므로 색의 명암에서 높이·노멀을 만들지 않는다.
        axes = node("MaterialExpressionComponentMask", r=True, g=True, b=False, a=False)
        link(position, axes, "")
        uv = node("MaterialExpressionDivide", const_b=180. if painted else 60.)
        link(axes, uv, "A")
        base = node("MaterialExpressionTextureSample", texture=albedo)
        link(uv, base, "UVs")
        rough = scalar(.36 if finish == "granite" else .34)
        if painted:
            # 도장 벽은 알베도 한 장만 쓴다. 거친 미장면의 노멀·오클루전은 남기지 않는다.
            # 벽 도장은 시안 색 #D5CEBE(선형 0.673, 0.625, 0.523)에 맞춰 회색 원본(선형 0.505, 0.491,
            # 0.442)을 크림색으로 올린다. 그대로 두면 복도 등 아래에서 회녹색(#73766A)으로 읽혔다
            # (2026-10-19). 허리 아래 칠은 이미 #73796B 언저리라 그대로 둔다.
            tint = node("MaterialExpressionMultiply")
            link(base, tint, "A")
            paint = {"landing_dado": (.44, .48, .46), "landing_wall": (1.33, 1.27, 1.18)}
            link(color(paint.get(finish, (1., 1., 1.))), tint, "B")
            base = tint
            rough = scalar(.62 if finish == "landing_dado" else .88)
    if finish != "wall" and not painted:
        # 반복 전 좌표의 미분으로 픽셀 면적을 구한다. frac의 경계에서 미분하면
        # 줄눈 한 줄이 타일 너비만큼 번지거나 먼 바닥에서 반짝인다.
        grid = node("MaterialExpressionCustom", output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                    code="float2 uv=P.xy/60.0; float2 w=max(fwidth(uv),1e-5); float2 d=abs(frac(uv+0.5)-0.5); float2 a=saturate((0.001667-d)/w+0.5); a=lerp(a,float2(0.003333,0.003333),saturate(w*2.0-1.0)); return 1.0-(1.0-a.x)*(1.0-a.y);",
                    description="600mm 타일 / 2mm 줄눈")
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", "P")
        grid.set_editor_property("inputs", [pin])
        link(position, grid, "P")
        if finish == "ceiling":
            base = color((.73, .735, .705))
            rough = scalar(.82)
        seam = color((.24, .245, .235) if finish in ("floor", "granite") else (.57, .58, .55))
        blend = node("MaterialExpressionLinearInterpolate")
        link(base, blend, "A")
        link(seam, blend, "B")
        link(grid, blend, "Alpha")
        base = blend
        rough_mix = node("MaterialExpressionLinearInterpolate")
        link(rough, rough_mix, "A")
        link(scalar(.78), rough_mix, "B")
        link(grid, rough_mix, "Alpha")
        rough = rough_mix
    if grime:
        # 사람이 오래 산 빌라 공용부. 벽은 누렇게 바래고 위에서 물때가 흐르고,
        # 천장에는 물이 번졌다 마른 테두리가 남고, 바닥은 발길이 모이는 곳이 탁하다.
        # 텍스처는 회색(0.5)이 곱하기 1인 색 오버레이다. 두 배 해서 원래 색에 곱하되,
        # 세기(G)만큼 회색에서 멀어지게 키운다.
        _, tile, gain, rough_shift = GRIME[finish]
        grime_axes = node("MaterialExpressionComponentMask", r=True, g=True, b=False, a=False)
        link(position, grime_axes, "")
        grime_uv = node("MaterialExpressionDivide", const_b=tile)
        link(grime_axes, grime_uv, "A")
        overlay = node("MaterialExpressionTextureSample", texture=grime,
                       sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        link(grime_uv, overlay, "UVs")

        def custom(code, labels, output, description):
            expression = node("MaterialExpressionCustom", output_type=output, code=code,
                              description=description)
            pins = []
            for label in labels:
                pin = unreal.CustomInput()
                pin.set_editor_property("input_name", label)
                pins.append(pin)
            expression.set_editor_property("inputs", pins)
            return expression

        where = custom(GRIME_PLACE[finish], ("P", "G"), unreal.CustomMaterialOutputType.CMOT_FLOAT1, "때 자리")
        link(node("MaterialExpressionWorldPosition"), where, "P")
        link(scalar(gain), where, "G")
        tinted = custom("return B*saturate(1.0-(1.0-saturate(M.rgb*2.0))*G);",
                        ("B", "M", "G"), unreal.CustomMaterialOutputType.CMOT_FLOAT3, "때")
        link(base, tinted, "B")
        link(overlay, tinted, "M")
        link(where, tinted, "G")
        base = tinted
        if rough_shift:
            shifted = custom(
                "float3 f=saturate(1.0-(1.0-saturate(M.rgb*2.0))*G);"
                "return saturate(R+(1.0-dot(f,1.0/3.0))*S);",
                ("R", "M", "G", "S"), unreal.CustomMaterialOutputType.CMOT_FLOAT1, "때 거칠기")
            link(rough, shifted, "R")
            link(overlay, shifted, "M")
            link(where, shifted, "G")
            link(scalar(rough_shift), shifted, "S")
            rough = shifted
        if finish == "granite":
            # 발길이 모이는 복도 가운데는 연마한 광이 닳아 덜 비친다.
            worn = custom("return saturate(R+0.10*saturate(1.0-abs(P.y+305.0)/40.0));",
                          ("R", "P"), unreal.CustomMaterialOutputType.CMOT_FLOAT1, "닳은 길")
            link(rough, worn, "R")
            link(node("MaterialExpressionWorldPosition"), worn, "P")
            rough = worn
    lib.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    # 기존 그래프의 노드를 지워도 출력 핀 참조는 남을 수 있어 모두 다시 연결한다.
    lib.connect_material_property(color((0., 0., 1.)), "", unreal.MaterialProperty.MP_NORMAL)
    lib.connect_material_property(scalar(0.), "", unreal.MaterialProperty.MP_METALLIC)
    lib.connect_material_property(scalar(1.), "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    lib.connect_material_property(color((0., 0., 0.)), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    marker = node("MaterialExpressionScalarParameter", parameter_name=MARKER, default_value=1.)
    specular = node("MaterialExpressionMultiply")
    link(marker, specular, "A")
    link(scalar(.45), specular, "B")
    lib.connect_material_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)
    lib.layout_material_expressions(material)
    lib.recompile_material(material)
    return material
