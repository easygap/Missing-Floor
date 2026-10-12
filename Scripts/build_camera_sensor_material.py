"""표면과 글씨를 흐리지 않는 가벼운 렌즈 후처리.

화면 가장자리의 약한 통 왜곡과 색 번짐만 남긴다. 전면 노이즈, 흐림,
과도한 샤프닝은 재질의 작은 요철과 문서 글씨를 덮으므로 사용하지 않는다.
톤매핑 뒤에서 동작하며 HUD에는 적용되지 않는다. 렌즈 왜곡을 0으로
내리면 UIGCameraSensorComponent가 후처리 패스 자체를 끈다.
"""

from pathlib import Path
import sys
import unreal

ROOT = Path(unreal.SystemLibrary.get_project_directory())
sys.path.insert(0, str(ROOT / "Scripts"))
LIB = unreal.MaterialEditingLibrary
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
NAME = "M_PP_CameraSensor"
FOLDER = "/Game/Prototype/Materials"

# 기본값이 곧 「렌즈 왜곡 100%」다. 조절은 Strength 하나로 한다.
PARAMETERS = (
    ("Strength", 1.0),
    ("Distortion", 0.016),
    ("Fringe", 0.35),
)

CODE = r"""
float s = saturate(Strength);
float4 viewSize = GetSceneTextureViewSize(PPI_PostProcessInput0);
float2 inv = viewSize.zw;
float2 vuv = GetViewportUV(Parameters);
float aspect = viewSize.x * viewSize.w;

struct FCameraTap
{
	float3 At(float2 ViewportUV)
	{
		float2 BufferUV = ClampSceneTextureUV(
			ViewportUVToSceneTextureUV(ViewportUV, PPI_PostProcessInput0),
			PPI_PostProcessInput0);
		return SceneTextureLookup(BufferUV, PPI_PostProcessInput0, true).rgb;
	}
};
FCameraTap tap;

// 통 왜곡. 모서리가 화면 밖으로 나가지 않게 전체를 조금 당긴다.
float2 c = vuv - 0.5;
float2 ca = float2(c.x * aspect, c.y);
float r2 = dot(ca, ca);
float cornerR2 = 0.25 * (aspect * aspect + 1.0);
float k = Distortion * s;
float2 duv = 0.5 + c * (1.0 + k * r2) / (1.0 + k * cornerR2);

// 가장자리로 갈수록 커지는 색 번짐. 화면 끝에서 Fringe 픽셀이다.
float2 fringe = c * 2.0 * Fringe * s * inv;
float3 centre = tap.At(duv);
float3 col = float3(tap.At(duv + fringe).r, centre.g, tap.At(duv - fringe).b);

return max(col, 0.0);
"""


def enum_value(owner, *names):
    for name in names:
        value = getattr(owner, name, None)
        if value is not None:
            return value
    raise RuntimeError(f"{owner.__name__}에 {names} 중 아무것도 없다")


def build():
    asset = unreal.load_asset(f"{FOLDER}/{NAME}")
    if not asset:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    for expression in list(LIB.get_material_expressions(asset)):
        LIB.delete_material_expression(asset, expression)

    asset.set_editor_property(
        "material_domain", enum_value(unreal.MaterialDomain, "MD_POST_PROCESS"))
    asset.set_editor_property(
        "blendable_location",
        enum_value(
            unreal.BlendableLocation,
            "BL_SCENE_COLOR_AFTER_TONEMAPPING",
            "BL_AFTER_TONEMAPPING"))

    def node(kind, **properties):
        result = LIB.create_material_expression(asset, getattr(unreal, kind))
        for key, value in properties.items():
            result.set_editor_property(key, value)
        return result

    def link(a, b, pin):
        if not LIB.connect_material_expressions(a, "", b, pin):
            raise RuntimeError(f"연결 실패: {pin}")

    custom = node(
        "MaterialExpressionCustom",
        code=CODE,
        output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        description="CameraSensor")
    pins = ["SceneTex"] + [name for name, _ in PARAMETERS]
    inputs = []
    for label in pins:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", label)
        inputs.append(pin)
    custom.set_editor_property("inputs", inputs)

    # 입력값 자체는 쓰지 않는다. 이 노드가 있어야 셰이더가 PostProcessInput0을 묶는다.
    scene = node(
        "MaterialExpressionSceneTexture",
        scene_texture_id=enum_value(
            unreal.SceneTextureId, "PPI_POST_PROCESS_INPUT0"))
    link(scene, custom, "SceneTex")
    for name, default in PARAMETERS:
        parameter = node(
            "MaterialExpressionScalarParameter",
            parameter_name=name,
            default_value=default)
        link(parameter, custom, name)

    LIB.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    LIB.layout_material_expressions(asset)
    LIB.recompile_material(asset)
    ASSETS.save_loaded_asset(asset)
    unreal.log(
        f"CAMERA_SENSOR_MATERIAL PASS domain={asset.get_editor_property('material_domain')} "
        f"location={asset.get_editor_property('blendable_location')}")


build()
