"""Builds the live browser preview of an animated figure in one go (see FIGUR_ANIMIEREN.md).

    python tools/mascot/build_live_preview.py <figure_without_eyes.png> <out.html> [--ref <figure_with_eyes.png>]

1. rig_ember.py --no-eyes   cuts the figure into parts + meshes (into <out>_rig/)
2. eye_design.py            writes the vector eyes and brows (DESIGN in eye_design.py) and a preview sheet
3. fills ember_live_prototype_template.html with the rig, the part pictures and the eyes -> one self-contained HTML

The HTML has the whole behaviour (brain) and the test hook: in the browser console
    window.__emberPose = true; window.__ember.<joint>.v = <value>
freezes the brain so poses can be checked one by one.
"""
import base64
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import rig_ember  # noqa: E402
import eye_design  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def main(src, out_html, ref=None):
    base = os.path.splitext(out_html)[0]
    rig_dir = base + '_rig'
    rig_ember.main(src, rig_dir, no_eyes=True)
    eyes_json = os.path.join(rig_dir, 'eyes.json')
    eye_design.main(src, eyes_json, ref, base + '_eyes_preview.png')
    rig = json.load(open(os.path.join(rig_dir, 'rig.json')))
    eyes = json.load(open(eyes_json))
    imgs = {n: 'data:image/png;base64,' + base64.b64encode(open(os.path.join(rig_dir, n + '.png'), 'rb').read()).decode()
            for n in rig['parts']}
    tpl = open(os.path.join(HERE, 'ember_live_prototype_template.html'), encoding='utf-8').read()
    html = (tpl.replace('__RIG__', json.dumps(rig, separators=(',', ':')))
               .replace('__IMAGES__', json.dumps(imgs))
               .replace('__EYES__', json.dumps(eyes, separators=(',', ':'))))
    open(out_html, 'w', encoding='utf-8').write(html)
    print('preview:', out_html, f'({len(html) // 1024} KB)')


if __name__ == '__main__':
    a = sys.argv[1:]
    main(a[0], a[1], a[a.index('--ref') + 1] if '--ref' in a else None)
