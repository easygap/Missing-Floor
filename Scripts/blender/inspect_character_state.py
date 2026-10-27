"""캐릭터의 치수·뼈·표정 리그를 읽어 표준 출력으로 보낸다. 원본은 저장하지 않는다.

blender -b --factory-startup --python inspect_character_state.py -- <원본.blend>
"""

import argparse
import json
import sys

import bpy
from mathutils import Vector


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    bpy.ops.wm.open_mainfile(filepath=args.source)
    for obj in bpy.context.scene.objects:
        if obj.type not in ('MESH', 'ARMATURE'):
            continue
        state = {
            'name': obj.name,
            'type': obj.type,
            'scale': list(obj.scale),
            'rotation': list(obj.rotation_euler),
            'parent': obj.parent.name if obj.parent else None,
        }
        if obj.type == 'ARMATURE':
            state['bones'] = [
                {'name': bone.name,
                 'parent': bone.parent.name if bone.parent else None,
                 'head': list(bone.head_local), 'tail': list(bone.tail_local)}
                for bone in obj.data.bones
            ]
        else:
            state.update(
                vertices=len(obj.data.vertices),
                triangles=sum(len(face.vertices) - 2 for face in obj.data.polygons),
                smooth_faces=sum(face.use_smooth for face in obj.data.polygons),
                modifiers=[modifier.type for modifier in obj.modifiers],
                materials=[material.name for material in obj.data.materials if material],
                shape_keys=[key.name for key in obj.data.shape_keys.key_blocks]
                if obj.data.shape_keys else [],
                bounds=[list(obj.matrix_world @ Vector(corner)) for corner in obj.bound_box],
            )
        print('CHARACTER_STATE ' + json.dumps(state, ensure_ascii=False))


if __name__ == '__main__':
    main()
