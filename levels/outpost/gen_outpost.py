#!/usr/bin/env python3
"""Generates the Outpost map: an original desert, defuse-style arena.

Outputs into areas/1/: model.inc.c (vertex-colored display list), collision.inc.c and
objects.inc.c (trooper / crate / pickup placements). Coordinates: X east, Z south, Y up.
Everything stays inside the engine's +/-8192 level boundary.
"""
import os, random

HERE = os.path.dirname(os.path.abspath(__file__))
rng = random.Random(7)

COLORS = {
    'sand':      (0xD9, 0xBC, 0x86),
    'sandstone': (0xC9, 0xA3, 0x6E),
    'trim':      (0x9C, 0x7A, 0x4E),
    'wall':      (0xB8, 0x92, 0x60),
    'crate':     (0x8E, 0x62, 0x30),
    'crate2':    (0x6E, 0x4C, 0x26),
    'stone':     (0xA8, 0xA0, 0x90),
    'door':      (0x2E, 0x58, 0x92),
    'paint':     (0xE0, 0x6A, 0x20),
    'platform':  (0xBE, 0xA4, 0x7A),
}

boxes = []   # (x0, x1, y0, y1, z0, z1, color, solid)
ramps = []   # (x0, x1, z0, z1, h, direction, color)  height rises toward 'direction'

def B(x0, x1, y0, y1, z0, z1, color='sandstone', solid=True):
    boxes.append((min(x0, x1), max(x0, x1), y0, y1, min(z0, z1), max(z0, z1), color, solid))

WALL_H = 1000
# ---- perimeter
B(-6200, 6200, 0, WALL_H, -6200, -6000, 'wall')
B(-6200, 6200, 0, WALL_H, 6000, 6200, 'wall')
B(-6200, -6000, 0, WALL_H, -6000, 6000, 'wall')
B(6000, 6200, 0, WALL_H, -6000, 6000, 'wall')

# ---- central building blocks (lanes run between them)
# west block (between long lane and mid), split by the mid doors at z 0..700
B(-3200, -900, 0, 900, -2400, 0)
B(-3200, -900, 0, 900, 700, 3600)
# east block (between mid and the east lane), split by a connector at z 1200..1900
B(900, 3000, 0, 900, -2400, 1200)
B(900, 3000, 0, 900, 1900, 3600)
# far-west wall along the long lane with a corner pocket
B(-6000, -4600, 0, 700, 1200, 3600)
# far-east block narrowing the east lane
B(4700, 6000, 0, 800, -1200, 3600)

# ---- north: CT base between the sites
B(-1900, -1300, 0, 900, -4800, -2600)       # wall between A and CT, gap to the north
B(1300, 1900, 0, 900, -4800, -2600)         # wall between B and CT
B(-700, 700, 0, 400, -4200, -3600, 'stone') # low CT barricade

# ---- site A (northwest): raised platform with ramps
B(-5000, -2500, 0, 180, -5000, -3200, 'platform')
ramps.append((-4400, -3500, -3200, -2600, 180, 'north', 'platform'))
ramps.append((-2500, -1900, -4400, -3700, 180, 'west', 'platform'))
for (x, z, two) in ((-4500, -4500, True), (-3300, -3800, False), (-2900, -4600, True), (-4000, -3500, False)):
    B(x, x + 220, 180, 400, z, z + 220, 'crate')
    if two:
        B(x + 20, x + 200, 400, 600, z + 20, z + 200, 'crate2')
# painted A on the west wall facing the site
def letter_A(x, y, z):
    B(x, x + 6, y, y + 260, z - 100, z - 70, 'paint', False)
    B(x, x + 6, y, y + 260, z + 70, z + 100, 'paint', False)
    B(x, x + 6, y + 230, y + 260, z - 100, z + 100, 'paint', False)
    B(x, x + 6, y + 120, y + 150, z - 100, z + 100, 'paint', False)
letter_A(-5999, 300, -4100)

# ---- site B (northeast): courtyard with pillars and crate stacks
for (x, z) in ((2600, -4600), (4200, -4600), (2600, -3200), (4200, -3200)):
    B(x, x + 220, 0, 800, z, z + 220, 'stone')
for (x, z, two) in ((3300, -4000, True), (3600, -4000, False), (2200, -3800, True), (4700, -3900, False), (3400, -5200, True)):
    B(x, x + 240, 0, 240, z, z + 240, 'crate')
    if two:
        B(x + 20, x + 220, 240, 480, z + 20, z + 220, 'crate2')
def letter_B(x, y, z):
    B(x - 6, x, y, y + 260, z - 100, z - 70, 'paint', False)
    B(x - 6, x, y + 230, y + 260, z - 100, z + 80, 'paint', False)
    B(x - 6, x, y + 115, y + 145, z - 100, z + 80, 'paint', False)
    B(x - 6, x, y, y + 30, z - 100, z + 80, 'paint', False)
    B(x - 6, x, y + 30, y + 115, z + 60, z + 90, 'paint', False)
    B(x - 6, x, y + 145, y + 230, z + 60, z + 90, 'paint', False)
letter_B(5999, 300, -4100)

# ---- cover along the lanes
for (x, z) in ((-4300, 400), (-5200, -1200), (-3900, -2200), (-200, 1600), (300, -1400), (3600, 400), (3900, -1800), (1900, 4300), (-2200, 4500)):
    B(x, x + 220, 0, 220, z, z + 220, 'crate')
for (x0, x1, z0, z1) in ((-600, 600, 2600, 2700), (-5600, -4800, -600, -500), (3300, 4300, 2500, 2600), (-1000, 1000, -5200, -5100)):
    B(x0, x1, 0, 140, z0, z1, 'stone')  # low walls

# ---- decorations: blue doors and trim on building faces
for (x, z0, z1) in ((-3201, 1600, 1900), (-899, -1600, -1300), (2999, -800, -500), (899, 2600, 2900)):
    B(x - 3, x + 3, 0, 420, z0, z1, 'door', False)
for (x0, x1, z0, z1) in ((-3200, -900, -2400, -2400), (900, 3000, -2400, -2400), (-3200, -900, 3600, 3600), (900, 3000, 3600, 3600)):
    B(x0, x1, 860, 900, z0 - 6, z0 + 6, 'trim', False)

# ------------------------------------------------------------------ meshes
SUN = {'+y': 1.0, '+x': 0.82, '-x': 0.62, '+z': 0.9, '-z': 0.58, '-y': 0.4}

def shade(color, face, y, jitter=0.0):
    if isinstance(color, str):
        color = COLORS[color]
    k = SUN[face]
    if face != '+y':
        k *= 0.78 + 0.22 * min(1.0, y / 400.0)   # darker toward the ground (ambient occlusion feel)
    k *= 1.0 + jitter
    return tuple(max(0, min(255, int(c * k))) for c in color)

vis = []   # triangles: [(pos, rgb) x3]
col = []   # collision triangles: [pos x3]

def quad(p, face, color, solid, render=True):
    """p = 4 corners, counter-clockwise seen from outside."""
    j = rng.uniform(-0.04, 0.04)
    if render:
        cs = [shade(color, face, v[1], j) for v in p]
        vis.append([(p[0], cs[0]), (p[1], cs[1]), (p[2], cs[2])])
        vis.append([(p[0], cs[0]), (p[2], cs[2]), (p[3], cs[3])])
    if solid:
        col.append([p[0], p[1], p[2]])
        col.append([p[0], p[2], p[3]])

for (x0, x1, y0, y1, z0, z1, color, solid) in boxes:
    quad([(x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0)], '+y', color, solid)
    quad([(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)], '+x', color, solid)
    quad([(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)], '-x', color, solid)
    quad([(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], '+z', color, solid)
    quad([(x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0)], '-z', color, solid)

for (x0, x1, z0, z1, h, d, color) in ramps:
    if d == 'north':   # high edge at z0
        top = [(x0, h, z0), (x0, 0, z1), (x1, 0, z1), (x1, h, z0)]
        quad(top, '+y', color, True)
        quad([(x0, 0, z0), (x0, 0, z1), (x0, h, z0), (x0, h, z0)][:3] + [(x0, h, z0)], '-x', color, False)
        quad([(x1, 0, z0), (x1, h, z0), (x1, h, z0), (x1, 0, z1)], '+x', color, False)
    else:              # 'west': high edge at x0
        top = [(x0, h, z0), (x0, h, z1), (x1, 0, z1), (x1, 0, z0)]
        quad(top, '+y', color, True)

# ground: tiled for color variation and smaller collision triangles
T = 1000
for xi in range(-6000, 6000, T):
    for zi in range(-6000, 6000, T):
        quad([(xi, 0, zi), (xi, 0, zi + T), (xi + T, 0, zi + T), (xi + T, 0, zi)], '+y', COLORS['sand'], True)

# ------------------------------------------------------------------ emit display list
def emit_dl():
    lines, verts, batch, cur = [], [], [], {}
    def flush():
        if not batch: return
        start = len(verts)
        for k, _ in sorted(cur.items(), key=lambda kv: kv[1]): verts.append(k)
        lines.append(f"    gsSPVertex(outpost_area1_vtx + {start}, {len(cur)}, 0),")
        for i in range(0, len(batch), 2):
            if i + 1 < len(batch):
                a, b = batch[i], batch[i + 1]
                lines.append(f"    gsSP2Triangles({a[0]}, {a[1]}, {a[2]}, 0x0, {b[0]}, {b[1]}, {b[2]}, 0x0),")
            else:
                a = batch[i]; lines.append(f"    gsSP1Triangle({a[0]}, {a[1]}, {a[2]}, 0x0),")
        batch.clear(); cur.clear()
    for t in vis:
        keys = [(tuple(int(c) for c in p), rgb) for p, rgb in t]
        new = [k for k in dict.fromkeys(keys) if k not in cur]
        if len(cur) + len(new) > 16: flush()
        for k in keys:
            if k not in cur: cur[k] = len(cur)
        batch.append(tuple(cur[k] for k in keys))
    flush()
    vtx = ["// Outpost - generated by gen_outpost.py. Do not edit by hand.\n",
           "static const Vtx outpost_area1_vtx[] = {"]
    vtx += [f"    {{{{{{ {p[0]}, {p[1]}, {p[2]} }}, 0, {{ 0, 0 }}, {{ 0x{c[0]:02x}, 0x{c[1]:02x}, 0x{c[2]:02x}, 0xff }}}}}}," for p, c in verts]
    vtx.append("};\n")
    dl = ["const Gfx outpost_area1_dl[] = {", "    gsDPPipeSync(),", "    gsSPClearGeometryMode(G_LIGHTING),",
          "    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),", "    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),"]
    dl += lines
    dl += ["    gsDPPipeSync(),", "    gsSPSetGeometryMode(G_LIGHTING),", "    gsSPEndDisplayList(),", "};\n"]
    return "\n".join(vtx) + "\n" + "\n".join(dl), len(verts)

model, nverts = emit_dl()
open(os.path.join(HERE, 'areas/1/model.inc.c'), 'w').write(model)

# ------------------------------------------------------------------ emit collision
cv, cidx = [], {}
ctris = []
for t in col:
    ids = []
    for p in t:
        k = tuple(int(c) for c in p)
        if k not in cidx:
            cidx[k] = len(cv); cv.append(k)
        ids.append(cidx[k])
    if len(set(ids)) == 3:
        ctris.append(ids)
c = ["// Outpost collision - generated by gen_outpost.py\n", "const Collision outpost_seg7_collision[] = {", "    COL_INIT(),",
     f"    COL_VERTEX_INIT({len(cv)}),"]
c += [f"    COL_VERTEX({x}, {y}, {z})," for x, y, z in cv]
c += [f"    COL_TRI_INIT(SURFACE_DEFAULT, {len(ctris)}),"]
c += [f"    COL_TRI({a}, {b}, {d})," for a, b, d in ctris]
c += ["    COL_TRI_STOP(),", "    COL_END(),", "};\n"]
open(os.path.join(HERE, 'areas/1/collision.inc.c'), 'w').write("\n".join(c))

# ------------------------------------------------------------------ objects
troopers = [  # x, z, yaw (degrees; 0 = facing +Z/south)
    (-3800, -4000, 0), (-2800, -3500, 30), (-4600, -1600, 0), (-4200, 1800, 10),
    (0, -4600, 0), (-300, -1900, 0), (400, 900, 180), (-1600, 300, 90),
    (3400, -3700, 0), (2400, -4500, 0), (4200, -2400, 330), (3800, 1500, 0),
    (2000, 1550, 270), (-5200, 4200, 90),
]
crates = [(-5400, 2600), (5400, -400), (-200, -3000), (2100, 3000), (-2400, -3000)]
hearts = [(0, 4600), (-1000, -3900), (3800, -1000)]
o = ["// Outpost objects - generated by gen_outpost.py"]
for x, z, yaw in troopers:
    o.append(f"OBJECT(/*model*/ MODEL_DESERT_TROOPER, /*pos*/ {x}, 60, {z}, /*angle*/ 0, {yaw}, 0, /*bhvParam*/ 0, /*bhv*/ bhvDesertTrooper),")
for x, z in crates:
    o.append(f"OBJECT(/*model*/ MODEL_BREAKABLE_BOX, /*pos*/ {x}, 0, {z}, /*angle*/ 0, 0, 0, /*bhvParam*/ 0, /*bhv*/ bhvBreakableBox),")
for x, z in hearts:
    o.append(f"OBJECT(/*model*/ MODEL_HEART, /*pos*/ {x}, 200, {z}, /*angle*/ 0, 0, 0, /*bhvParam*/ 0, /*bhv*/ bhvRecoveryHeart),")
open(os.path.join(HERE, 'areas/1/objects.inc.c'), 'w').write("\n".join(o) + "\n")

print(f"outpost: {len(boxes)} boxes, {len(vis)} visible tris, {nverts} vertices, {len(ctris)} collision tris, {len(troopers)} troopers")
