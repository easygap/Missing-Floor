"""엔진 구름 재질을 게임용 하늘로 복제한다. 엔진 원본은 바꾸지 않는다."""

import unreal

assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
path = '/Game/Prototype/Materials/M_OvercastSky'
material = unreal.load_asset(path)
if not material:
    material = assets.duplicate_asset('/Engine/EngineSky/M_Sky_Panning_Clouds2', path)
if not isinstance(material, unreal.Material):
    raise RuntimeError('구름 재질을 만들 수 없습니다.')
# 대기 산란이 이미 계산된 하늘에 다시 산란을 더하지 않는다.
# 실시간 스카이라이트도 이 표면을 하늘로 인식해야 건물과 하늘의 색이 어긋나지 않는다.
material.set_editor_property('is_sky', True)
material.set_editor_property('two_sided', True)
material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
unreal.MaterialEditingLibrary.recompile_material(material)
assets.save_loaded_asset(material)
unreal.log('OVERCAST_SKY PASS is_sky=true engine_original_unchanged=true')
