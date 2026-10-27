"""Render the Vocalypse devil mascot from a Meshy GLB (model + rig + all animations).

Run headless with Blender 5.x:
    blender -b --factory-startup -P tools/mascot/render_mascot.py -- --glb <file.glb> --out <dir> [options]

Modes:
    sheet   one still per animation (middle frame) -> <out>/sheet/<anim>.png
    loop    PNG sequence with alpha for one animation -> <out>/<anim>/####.png
    list    print the animations and their frame ranges
"""

import argparse
import math
import os
import sys

import bpy
from mathutils import Vector

FPS = 30


# Defaults = look "H" (closest to concepts/RedDevil.png), 60 fps, one smoothing pass; chosen 2026-10-27.
def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--glb", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--mode", choices=["sheet", "loop", "list"], default="sheet")
    p.add_argument("--anim", default="All_Night_Dance")
    p.add_argument("--size", type=int, default=800)
    p.add_argument("--step", type=int, default=1, help="render every n-th frame (loop mode)")
    p.add_argument("--samples", type=int, default=32)
    p.add_argument("--light", choices=sorted(LIGHTS), default="deep")
    p.add_argument("--view", choices=["standard", "agx-punchy", "pbr-neutral"], default="pbr-neutral")
    p.add_argument("--exposure", type=float, default=-1.5)
    p.add_argument("--tex-value", type=float, default=0.75, help="brightness factor on the colour texture")
    p.add_argument("--tex-hue", type=float, default=0.008, help="hue shift on the colour texture (+ = towards orange)")
    p.add_argument("--tex-sat", type=float, default=1.35, help="saturation factor on the colour texture")
    p.add_argument("--gloss", type=float, default=0.7, help="roughness factor (<1 = shinier vinyl)")
    p.add_argument("--coat", type=float, default=0.0, help="clear-coat weight (vinyl-toy shine)")
    p.add_argument("--coat-rough", type=float, default=0.15)
    p.add_argument("--rate", type=int, default=60, help="output fps; above 30 renders in-between subframes")
    p.add_argument("--smooth", type=int, default=1, help="passes of [1 2 1] smoothing on the motion curves")
    return p.parse_args(argv)


# Light rigs: (name, position as (side, front, up) in character heights, colour, power, size).
# side +x = character's left (camera right), front -y = towards camera.
LIGHTS = {
    # Neutral softbox studio: true texture colours, like a product shot of a vinyl toy.
    "studio": {
        "world": ((0.5, 0.5, 0.5), 0.35),
        "rigs": [
            ("Key", (-1.0, -1.6, 1.4), (1.0, 1.0, 1.0), 140, 1.6),
            ("Fill", (1.4, -1.2, 0.7), (1.0, 1.0, 1.0), 45, 2.0),
            ("Rim", (0.6, 1.4, 1.5), (1.0, 1.0, 1.0), 120, 0.8),
        ],
    },
    # Warm key, cool fill: Pixar-like, slightly more mood.
    "warm": {
        "world": ((0.45, 0.47, 0.55), 0.3),
        "rigs": [
            ("Key", (-1.0, -1.6, 1.4), (1.0, 0.92, 0.82), 150, 1.6),
            ("Fill", (1.4, -1.2, 0.7), (0.8, 0.88, 1.0), 40, 2.0),
            ("Rim", (0.6, 1.4, 1.5), (1.0, 0.95, 0.9), 130, 0.8),
        ],
    },
    # Low ambient, strong key, weak fill: more modelling on the face, deeper red.
    "deep": {
        "world": ((0.45, 0.47, 0.55), 0.08),
        "rigs": [
            ("Key", (-1.0, -1.6, 1.4), (1.0, 0.93, 0.85), 170, 1.4),
            ("Fill", (1.4, -1.2, 0.7), (0.8, 0.88, 1.0), 15, 2.0),
            ("Rim", (0.6, 1.4, 1.5), (1.0, 0.95, 0.9), 110, 0.8),
        ],
    },
    # Neutral front light, a single cyan edge to tie in with the plugin palette.
    "cyan": {
        "world": ((0.5, 0.5, 0.5), 0.3),
        "rigs": [
            ("Key", (-1.0, -1.6, 1.4), (1.0, 1.0, 1.0), 140, 1.6),
            ("Fill", (1.4, -1.2, 0.7), (1.0, 1.0, 1.0), 40, 2.0),
            ("Rim", (1.1, 1.2, 1.3), (0.35, 0.9, 1.0), 200, 0.6),
        ],
    },
}


def setup_scene(size, samples, light, view, exposure):
    scene = bpy.context.scene
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    scene.render.fps = FPS
    engines = {e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items}
    scene.render.engine = "BLENDER_EEVEE" if "BLENDER_EEVEE" in engines else "BLENDER_EEVEE_NEXT"
    scene.eevee.taa_render_samples = samples
    scene.render.resolution_x = size
    scene.render.resolution_y = size
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    # Plain AgX pulls saturated red towards salmon pink; Standard keeps the texture's red.
    if view == "standard":
        scene.view_settings.view_transform = "Standard"
        scene.view_settings.look = "None"
    elif view == "pbr-neutral":
        # Made for product colours: keeps hue, rolls off highlights instead of clipping them.
        scene.view_settings.view_transform = "Khronos PBR Neutral"
        scene.view_settings.look = "None"
    else:
        scene.view_settings.view_transform = "AgX"
        scene.view_settings.look = "AgX - Punchy"
    scene.view_settings.exposure = exposure
    world = bpy.data.worlds.new("Stage") if scene.world is None else scene.world
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    color, strength = LIGHTS[light]["world"]
    bg.inputs["Color"].default_value = (*color, 1.0)
    bg.inputs["Strength"].default_value = strength
    return scene


def import_model(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    arm = next(o for o in new if o.type == "ARMATURE")
    # Only skinned meshes: the importer also adds a hidden Icosphere as bone display shape.
    meshes = [o for o in new if o.type == "MESH" and any(m.type == "ARMATURE" for m in o.modifiers)]
    if arm.animation_data is None:
        arm.animation_data_create()
    for track in list(arm.animation_data.nla_tracks):
        arm.animation_data.nla_tracks.remove(track)
    return arm, meshes


def tweak_materials(meshes, value, sat, gloss, hue=0.0, coat=0.0, coat_rough=0.15):
    """Optional grading of the baked Meshy textures inside the shader (texture files stay untouched)."""
    for m in meshes:
        for slot in m.material_slots:
            mat = slot.material
            if mat is None or not mat.use_nodes:
                continue
            nt = mat.node_tree
            bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
            if value != 1.0 or sat != 1.0 or hue != 0.0:
                link = bsdf.inputs["Base Color"].links[0]
                hsv = nt.nodes.new("ShaderNodeHueSaturation")
                hsv.inputs["Hue"].default_value = 0.5 + hue
                hsv.inputs["Saturation"].default_value = sat
                hsv.inputs["Value"].default_value = value
                nt.links.new(link.from_socket, hsv.inputs["Color"])
                nt.links.new(hsv.outputs["Color"], bsdf.inputs["Base Color"])
            if coat > 0.0:
                bsdf.inputs["Coat Weight"].default_value = coat
                bsdf.inputs["Coat Roughness"].default_value = coat_rough
            if gloss != 1.0:
                rough = bsdf.inputs["Roughness"]
                mul = nt.nodes.new("ShaderNodeMath")
                mul.operation = "MULTIPLY"
                mul.inputs[1].default_value = gloss
                if rough.links:
                    nt.links.new(rough.links[0].from_socket, mul.inputs[0])
                else:
                    mul.inputs[0].default_value = rough.default_value
                nt.links.new(mul.outputs[0], rough)


def fcurves(act):
    if hasattr(act, "layers"):
        for layer in act.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    yield from bag.fcurves
    else:
        yield from act.fcurves


def smooth_action(act, passes, wrap):
    """[1 2 1]/4 smoothing of every key value; wrap = the action is a loop (first key == last key)."""
    for fc in fcurves(act):
        kp = fc.keyframe_points
        n = len(kp)
        if n < 3:
            continue
        vals = [k.co[1] for k in kp]
        for _ in range(passes):
            if wrap:
                core = vals[:-1]
                m = len(core)
                core = [(core[i - 1] + 2 * core[i] + core[(i + 1) % m]) / 4 for i in range(m)]
                vals = core + [core[0]]
            else:
                vals = [vals[0]] + [(vals[i - 1] + 2 * vals[i] + vals[i + 1]) / 4 for i in range(1, n - 1)] + [vals[-1]]
        for k, v in zip(kp, vals):
            k.co[1] = v
            k.handle_left[1] = v
            k.handle_right[1] = v
        fc.update()


def actions():
    return sorted(bpy.data.actions, key=lambda a: a.name)


def find_action(name):
    for act in bpy.data.actions:
        if act.name == name or act.name.startswith(name) or name in act.name:
            return act
    raise SystemExit(f"animation '{name}' not found; have: {[a.name for a in bpy.data.actions]}")


def assign(arm, act):
    ad = arm.animation_data
    ad.action = act
    if hasattr(ad, "action_slot") and ad.action_slot is None and len(act.slots):
        ad.action_slot = act.slots[0]
    start, end = (int(round(v)) for v in act.frame_range)
    return start, end


def world_bbox(meshes):
    deps = bpy.context.evaluated_depsgraph_get()
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for m in meshes:
        ev = m.evaluated_get(deps)
        for v in ev.to_mesh().vertices:
            p = ev.matrix_world @ v.co
            lo = Vector(map(min, lo, p))
            hi = Vector(map(max, hi, p))
        ev.to_mesh_clear()
    return lo, hi


def area_light(name, loc, target, color, power, size):
    data = bpy.data.lights.new(name, "AREA")
    data.color = color
    data.energy = power
    data.size = size
    obj = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(obj)
    obj.location = loc
    obj.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    return obj


def setup_camera_and_lights(lo, hi, light):
    """Front camera, slightly below the face; stage light like concepts/RedDevilWerbung.png."""
    height = hi.z - lo.z
    center = (lo + hi) / 2
    # glTF characters face -Y in Blender.
    cam_data = bpy.data.cameras.new("Cam")
    cam_data.lens = 85
    cam = bpy.data.objects.new("Cam", cam_data)
    bpy.context.scene.collection.objects.link(cam)
    bpy.context.scene.camera = cam
    # Frame with headroom for dance moves (arms up, jumps).
    frame_h = height * 1.4
    dist = frame_h / (2 * math.tan(cam_data.angle / 2))
    cam.location = (center.x, center.y - dist, lo.z + height * 0.42)
    look = Vector((center.x, center.y, lo.z + height * 0.55))
    cam.rotation_euler = (look - cam.location).to_track_quat("-Z", "Y").to_euler()

    h = height
    for name, (sx, sy, sz), color, power, size in LIGHTS[light]["rigs"]:
        area_light(name, (center.x + h * sx, center.y + h * sy, lo.z + h * sz), look, color, power * h * h, h * size)


def render_to(path):
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def main():
    args = parse_args()
    scene = setup_scene(args.size, args.samples, args.light, args.view, args.exposure)
    arm, meshes = import_model(os.path.abspath(args.glb))
    tweak_materials(meshes, args.tex_value, args.tex_sat, args.gloss, args.tex_hue, args.coat, args.coat_rough)

    if args.mode == "list":
        for act in actions():
            print(f"ANIM {act.name} {tuple(act.frame_range)}")
        return

    # Frame the camera on the rest-ish pose of the idle animation so all renders share one camera.
    start, _ = assign(arm, find_action("Short_Breathe"))
    scene.frame_set(start)
    lo, hi = world_bbox(meshes)
    setup_camera_and_lights(lo, hi, args.light)

    out = os.path.abspath(args.out)
    if args.mode == "sheet":
        for act in actions():
            start, end = assign(arm, act)
            scene.frame_set((start + end) // 2)
            render_to(os.path.join(out, "sheet", act.name + ".png"))
    else:
        act = find_action(args.anim)
        start, end = assign(arm, act)
        if args.smooth:
            smooth_action(act, args.smooth, wrap=True)
        folder = os.path.join(out, args.anim)
        # Meshy loops repeat the first pose on the last frame; drop it so playback has no hitch.
        # Above 30 fps, in-between poses come from the linear key interpolation (subframes).
        sub = max(1, args.rate // FPS)
        steps = range(0, (end - start) * sub, args.step)
        for i, k in enumerate(steps):
            scene.frame_set(start + k // sub, subframe=(k % sub) / sub)
            render_to(os.path.join(folder, f"{i:04d}.png"))


main()
