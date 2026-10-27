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
    return p.parse_args(argv)


def setup_scene(size, samples):
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
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - Medium High Contrast"
    world = bpy.data.worlds.new("Stage") if scene.world is None else scene.world
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    bg.inputs["Color"].default_value = (0.03, 0.02, 0.06, 1.0)
    bg.inputs["Strength"].default_value = 0.6
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


def setup_camera_and_lights(lo, hi):
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
    # Near-white key keeps the devil red; colour comes from fill and rims.
    area_light("Key", (center.x - h * 1.2, center.y - h * 1.6, lo.z + h * 1.6), look, (1.0, 0.9, 0.85), 160 * h * h, h * 1.2)
    area_light("Fill", (center.x + h * 1.5, center.y - h * 1.2, lo.z + h * 0.8), look, (0.6, 0.4, 1.0), 50 * h * h, h * 1.5)
    area_light("RimCyan", (center.x + h * 1.1, center.y + h * 1.2, lo.z + h * 1.3), look, (0.2, 0.9, 1.0), 260 * h * h, h * 0.6)
    area_light("RimPink", (center.x - h * 1.1, center.y + h * 1.0, lo.z + h * 1.1), look, (1.0, 0.25, 0.6), 160 * h * h, h * 0.6)


def render_to(path):
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def main():
    args = parse_args()
    scene = setup_scene(args.size, args.samples)
    arm, meshes = import_model(os.path.abspath(args.glb))

    if args.mode == "list":
        for act in actions():
            print(f"ANIM {act.name} {tuple(act.frame_range)}")
        return

    # Frame the camera on the rest-ish pose of the idle animation so all renders share one camera.
    start, _ = assign(arm, find_action("Short_Breathe"))
    scene.frame_set(start)
    lo, hi = world_bbox(meshes)
    setup_camera_and_lights(lo, hi)

    out = os.path.abspath(args.out)
    if args.mode == "sheet":
        for act in actions():
            start, end = assign(arm, act)
            scene.frame_set((start + end) // 2)
            render_to(os.path.join(out, "sheet", act.name + ".png"))
    else:
        start, end = assign(arm, find_action(args.anim))
        folder = os.path.join(out, args.anim)
        for i, f in enumerate(range(start, end + 1, args.step)):
            scene.frame_set(f)
            render_to(os.path.join(folder, f"{i:04d}.png"))


main()
