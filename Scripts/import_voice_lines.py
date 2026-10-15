"""반출한 인물 목소리(VO_<줄 키>.wav)를 /Game/Audio/Voice/ko USoundWave로 반입한다.

Import-VoiceLines.ps1이 콘텐츠 전용 프로젝트(ArtImport)에서 돌린다. 게임은 IGVoice::Find로
VO_<NSLOCTEXT 키>를 찾고, 없으면 그 줄을 글로만 낸다.

목소리는 그 줄이 나올 때만 읽도록 스트리밍으로 두고(모든 장면의 목소리를 한꺼번에 메모리에
올리지 않는다), 받침과 짧은 숨이 뭉개지지 않게 압축 품질을 효과음보다 높게 둔다.

환경 변수
    IG_VOICE_SOURCE  manifest.json과 WAV가 있는 폴더(ASCII 경로)
"""

import json
import os

import unreal

VOICE_ROOT = "/Game/Audio/Voice/ko"
COMPRESSION_QUALITY = 80


def log(message):
    unreal.log_warning(f"[VOICE_IMPORT] {message}")


def set_property(asset, name, value):
    try:
        asset.set_editor_property(name, value)
        return True
    except Exception as error:  # noqa: BLE001 - 엔진 판마다 속성 이름이 다르다
        log(f"  property skipped {name}: {error}")
        return False


def main():
    source = os.environ.get("IG_VOICE_SOURCE")
    if not source or not os.path.isdir(source):
        raise RuntimeError(f"IG_VOICE_SOURCE is not a folder: {source}")
    with open(os.path.join(source, "manifest.json"), "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    imported = 0
    for entry in manifest["assets"]:
        name = entry["asset_name"]
        path = os.path.join(source, name + ".wav")
        if not os.path.isfile(path):
            raise RuntimeError(f"voice file missing: {path}")
        task = unreal.AssetImportTask()
        task.filename = path
        task.destination_path = VOICE_ROOT
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = False
        tools.import_asset_tasks([task])
        wave = unreal.load_asset(f"{VOICE_ROOT}/{name}")
        if wave is None:
            raise RuntimeError(f"voice import failed: {path}")
        set_property(wave, "looping", False)
        set_property(wave, "sound_group", unreal.SoundGroup.SOUNDGROUP_VOICE)
        set_property(wave, "compression_quality", COMPRESSION_QUALITY)
        set_property(wave, "loading_behavior", unreal.SoundWaveLoadingBehavior.LOAD_ON_DEMAND)
        unreal.EditorAssetLibrary.save_asset(f"{VOICE_ROOT}/{name}", False)
        imported += 1
        log(f"  {name} {entry.get('duration_seconds', 0)}s")
    log(f"VOICE_IMPORT PASS lines={imported}")


main()
