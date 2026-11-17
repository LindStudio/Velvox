"""Exports a built rig into the plugin's data folder (step 1 of FIGUR_ANIMIEREN.md, section 6).

    python tools/mascot/export_plugin_rig.py <rig dir from rig_ember.py> <eyes.json from eye_design.py> <plugin data dir>

Writes <plugin data dir>/rig.json (parts, meshes, joints, smile, the chosen eye variant only) and copies the part PNGs.
The plugin's CMake picks every file in that folder up as binary data (EmberRigData).
"""
import json
import os
import shutil
import sys


def main(rig_dir, eyes_json, out):
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith('.png') or f == 'rig.json':
            os.remove(os.path.join(out, f))
    rig = json.load(open(os.path.join(rig_dir, 'rig.json')))
    eyes = json.load(open(eyes_json))
    variant = eyes['variants'][eyes['default']]
    data = dict(size=rig['size'], parts=rig['parts'], meshes=rig['meshes'], joints=rig['joints'], smile=rig['smile'],
                eyes=dict(name=eyes['default'], colors=variant['colors'], eyes=variant['eyes']))
    json.dump(data, open(os.path.join(out, 'rig.json'), 'w'), separators=(',', ':'))
    for name in rig['parts']:
        shutil.copy(os.path.join(rig_dir, name + '.png'), os.path.join(out, 'part_' + name + '.png'))
    print('exported', len(rig['parts']), 'parts + rig.json (eyes:', eyes['default'] + ') to', out)


if __name__ == '__main__':
    main(*sys.argv[1:4])
