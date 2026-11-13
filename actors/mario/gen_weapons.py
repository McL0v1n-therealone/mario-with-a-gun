#!/usr/bin/env python3
"""Generates Mario's weapon models.

Outputs (next to this script):
  weapons.inc.c      - display lists for every weapon, moving parts, muzzle flash and projectiles (group0)
  weapons_geo.inc.c  - geo layouts that pick the current weapon and animate it (group0_geo)

Weapons are authored in a "logical" frame: +X = along the barrel (wrist -> knuckles of
Mario's fist bone), +Y = up, Z = sideways, origin at the wrist. The fist bone's "up" is
-Y, so Y is flipped (and triangle winding reversed) on output.

The weapon order here must match enum WeaponId in src/game/weapons.h.
Set GUN_SCALE=2 for oversized test models.
"""
import math, os

SCALE = float(os.environ.get('GUN_SCALE', '1'))
HERE = os.path.dirname(os.path.abspath(__file__))

COLORS = {
    'orange':   (0xF0, 0x6A, 0x10),
    'toydark':  (0x3C, 0x3C, 0x42),
    'polymer':  (0x26, 0x26, 0x29),
    'gunmetal': (0x46, 0x48, 0x4E),
    'steel':    (0x8C, 0x8E, 0x94),
    'blued':    (0x30, 0x34, 0x3C),
    'wood':     (0x7A, 0x44, 0x1E),
    'darkwood': (0x52, 0x2C, 0x14),
    'black':    (0x10, 0x10, 0x10),
    'rubber':   (0x18, 0x18, 0x18),
    'glass':    (0x20, 0x40, 0x60),
    'olive':    (0x4A, 0x52, 0x2E),
    'redtank':  (0xA8, 0x1C, 0x12),
    'ceramic':  (0xD2, 0xD6, 0xDC),
    'purple':   (0x3A, 0x26, 0x4C),
    'glowcyan': (0x40, 0xF0, 0xFF),
    'glowgreen': (0x70, 0xFF, 0x70),
}
GLOW = {'glowcyan', 'glowgreen'}

# ------------------------------------------------------------------ primitives (logical frame)
def rotz(p, a, c=(0, 0, 0)):
    x, y = p[0] - c[0], p[1] - c[1]
    return (c[0] + x * math.cos(a) - y * math.sin(a), c[1] + x * math.sin(a) + y * math.cos(a), p[2])

def box(center, half, rot=0.0):
    """Axis-aligned box, optionally rotated about Z (degrees) around its center."""
    a = math.radians(rot)
    cx, cy, cz = center
    hx, hy, hz = half
    faces = [
        ((1, 0, 0),  [(hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz), (hx, -hy, hz)]),
        ((-1, 0, 0), [(-hx, -hy, -hz), (-hx, -hy, hz), (-hx, hy, hz), (-hx, hy, -hz)]),
        ((0, 1, 0),  [(-hx, hy, -hz), (-hx, hy, hz), (hx, hy, hz), (hx, hy, -hz)]),
        ((0, -1, 0), [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz), (-hx, -hy, hz)]),
        ((0, 0, 1),  [(-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]),
        ((0, 0, -1), [(-hx, -hy, -hz), (-hx, hy, -hz), (hx, hy, -hz), (hx, -hy, -hz)]),
    ]
    tris = []
    for n, q in faces:
        pts = [rotz(p, a) for p in q]
        pts = [(p[0] + cx, p[1] + cy, p[2] + cz) for p in pts]
        nn = rotz(n, a)
        tris.append([(pts[0], nn), (pts[1], nn), (pts[2], nn)])
        tris.append([(pts[0], nn), (pts[2], nn), (pts[3], nn)])
    return tris

def prism(x0, x1, cy, cz, r, sides=8, smooth=True, phase=None, r1=None):
    """Cylinder-like prism along +X from x0 to x1 (optional taper to radius r1)."""
    r1 = r if r1 is None else r1
    if phase is None:
        phase = math.pi / sides
    ring = [(math.cos(phase + 2 * math.pi * k / sides), math.sin(phase + 2 * math.pi * k / sides)) for k in range(sides)]
    tris = []
    for k in range(sides):
        (ya, za), (yb, zb) = ring[k], ring[(k + 1) % sides]
        p00 = (x0, cy + ya * r, cz + za * r); p01 = (x0, cy + yb * r, cz + zb * r)
        p10 = (x1, cy + ya * r1, cz + za * r1); p11 = (x1, cy + yb * r1, cz + zb * r1)
        if smooth:
            na, nb = (0, ya, za), (0, yb, zb)
        else:
            m = ((ya + yb) / 2, (za + zb) / 2); l = math.hypot(*m)
            na = nb = (0, m[0] / l, m[1] / l)
        # outward winding: (p00, p01, p11) faces +radial when viewed from outside
        tris.append([(p00, na), (p10, na), (p11, nb)])
        tris.append([(p00, na), (p11, nb), (p01, nb)])
    for x, rr, n in ((x0, r, (-1, 0, 0)), (x1, r1, (1, 0, 0))):
        c = (x, cy, cz)
        for k in range(sides):
            (ya, za), (yb, zb) = ring[k], ring[(k + 1) % sides]
            pa = (x, cy + ya * rr, cz + za * rr); pb = (x, cy + yb * rr, cz + zb * rr)
            tris.append([(c, n), (pb, n), (pa, n)] if n[0] > 0 else [(c, n), (pa, n), (pb, n)])
    return tris

def fix_winding(tris):
    """Make every triangle's winding agree with its vertex normals (outward = CCW)."""
    out = []
    for t in tris:
        (pa, na), (pb, _), (pc, _) = t
        e1 = [pb[i] - pa[i] for i in range(3)]; e2 = [pc[i] - pa[i] for i in range(3)]
        fn = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        avg = [sum(v[1][i] for v in t) for i in range(3)]
        out.append(t if sum(fn[i] * avg[i] for i in range(3)) >= 0 else [t[0], t[2], t[1]])
    return out

# ------------------------------------------------------------------ weapon definitions
def grip(center, half, rot, color):
    return (color, box(center, half, rot))

def trigger_group(x, color_guard, color_trigger, depth=30):
    return [
        (color_guard,   box((x + 4, -depth, 0), (22, 3, 4))),          # guard bottom
        (color_guard,   box((x + 23, -depth / 2 - 2, 0), (3, depth / 2 - 1, 4))),  # guard front
        (color_trigger, box((x - 2, -12, 0), (3, 10, 3), -10)),        # trigger
    ]

W = []  # list of dicts, order = enum WeaponId

# 0: toy light gun (the original "Zapper" style piece)
W.append(dict(name='lightgun', body=[
    ('orange',  box((115, 17, 0), (85, 17, 15))),
    ('orange',  box((211, 17, 0), (11, 21, 19))),
    ('black',   box((223, 17, 0), (2, 12, 10))),
    ('toydark', box((135, 37, 0), (65, 3, 6))),
    ('toydark', box((26, 17, 0), (4, 15, 13))),
    grip((58, -34, 0), (20, 36, 13), -20, 'orange'),
    ('toydark', box((58 - 13, -34 - 36, 0), (21, 4, 14), -20)),
] + trigger_group(92, 'toydark', 'toydark'), part=None, muzzle=(226, 17, 0), flash=0))

# 1: pistol - polymer frame, steel slide that cycles back on every shot
W.append(dict(name='pistol', body=[
    ('polymer', box((118, 0, 0), (80, 7, 10))),                 # frame / dust cover
    ('steel',   prism(196, 206, 19, 0, 6, 8)),                  # barrel crown
    grip((60, -36, 0), (19, 34, 12), -18, 'polymer'),
    ('black',   box((60 - 11, -36 - 34, 0), (20, 3, 12.5), -18)),   # magazine base plate
] + trigger_group(92, 'polymer', 'steel'),
    part=dict(pivot=(0, 0, 0), geom=[
        ('gunmetal', box((116, 20, 0), (86, 14, 11))),          # slide
        ('black',    box((70, 20, 11.2), (12, 5, 0.6))),         # ejection port
        ('black',    box((40, 22, 11.2), (8, 9, 0.6))),          # rear serrations
        ('black',    box((40, 22, -11.2), (8, 9, 0.6))),
        ('steel',    box((190, 37, 0), (4, 3, 2))),              # front sight
        ('steel',    box((40, 37, 0), (5, 3, 6))),               # rear sight
    ]), muzzle=(208, 19, 0), flash=1))

# 2: magnum revolver - steel, wooden grip, cylinder turns after each shot
W.append(dict(name='magnum', body=[
    ('steel',   prism(118, 262, 24, 0, 9, 10)),                 # barrel
    ('steel',   box((190, 35, 0), (72, 4, 4))),                 # barrel rib
    ('steel',   box((255, 42, 0), (5, 4, 2))),                  # front sight
    ('steel',   box((58, 18, 0), (14, 24, 10))),                # rear frame
    ('steel',   box((95, 43, 0), (26, 4, 9))),                  # top strap
    ('steel',   box((95, -2, 0), (26, 5, 9))),                  # bottom strap
    ('steel',   box((36, 44, 0), (7, 8, 3), 20)),               # hammer
    grip((48, -38, 0), (16, 34, 12.5), -28, 'darkwood'),
] + trigger_group(84, 'steel', 'steel'),
    part=dict(pivot=(95, 20, 0), geom=[
        ('blued', prism(-22, 22, 0, 0, 21, 6, smooth=False)),   # cylinder
        ('black', prism(-23, -22, 0, 0, 8, 6)),                 # ratchet
    ]), muzzle=(264, 24, 0), flash=1))

# 3: pump shotgun - long barrel, mag tube, wooden furniture, pump racks after each shot
W.append(dict(name='shotgun', body=[
    ('blued',    prism(100, 430, 32, 0, 9, 10)),                # barrel
    ('steel',    box((420, 43, 0), (4, 2, 2))),                 # bead sight
    ('blued',    prism(100, 395, 12, 0, 8, 10)),                # magazine tube
    ('blued',    prism(395, 400, 12, 0, 9, 10)),                # mag cap
    ('gunmetal', box((50, 22, 0), (60, 22, 12))),               # receiver
    ('black',    box((60, 26, 12.2), (18, 7, 0.6))),            # ejection port
    ('wood',     box((-40, 22, 0), (30, 13, 10), -4)),          # stock wrist
    ('wood',     box((-125, 12, 0), (60, 26, 12), -8)),         # stock
    ('rubber',   box((-188, 5, 0), (4, 30, 13), -8)),           # recoil pad
    grip((30, -28, 0), (16, 28, 11), -24, 'wood'),
] + trigger_group(68, 'gunmetal', 'steel', depth=24),
    part=dict(pivot=(300, 12, 0), geom=[
        ('wood',  prism(-55, 55, 0, 0, 16, 10)),                # pump forend
        ('darkwood', prism(-40, -36, 0, 0, 16.5, 10)),          # grip grooves
        ('darkwood', prism(-10, -6, 0, 0, 16.5, 10)),
        ('darkwood', prism(20, 24, 0, 0, 16.5, 10)),
    ]), muzzle=(432, 32, 0), flash=2))

# 4: SMG - compact, folding wire stock, bolt handle kicks back
W.append(dict(name='smg', body=[
    ('polymer', box((85, 18, 0), (85, 18, 12))),                # receiver
    ('gunmetal', prism(170, 214, 20, 0, 10, 10)),               # barrel shroud
    ('black',   prism(214, 236, 20, 0, 7, 10)),                 # muzzle device
    ('polymer', box((14, 42, 0), (6, 6, 5))),                   # rear sight
    ('polymer', box((160, 42, 0), (5, 6, 3))),                  # front sight
    ('black',   box((122, -44, 0), (11, 38, 8.5), 8)),          # magazine
    grip((48, -32, 0), (16, 30, 11), -18, 'polymer'),
    ('gunmetal', box((-55, 30, 9), (55, 3, 2.5))),              # wire stock rails
    ('gunmetal', box((-55, 30, -9), (55, 3, 2.5))),
    ('rubber',  box((-113, 14, 0), (4, 22, 11))),               # butt plate
] + trigger_group(80, 'polymer', 'steel'),
    part=dict(pivot=(110, 26, 14), geom=[
        ('steel', box((0, 0, 3), (8, 4, 3))),                   # bolt handle
    ]), muzzle=(238, 20, 0), flash=1))

# 5: rifle - black modern rifle with optic, charging handle kicks back
W.append(dict(name='rifle', body=[
    ('polymer', box((75, 28, 0), (85, 16, 12))),                # upper receiver
    ('polymer', box((65, 4, 0), (55, 9, 11))),                  # lower receiver
    ('gunmetal', prism(160, 300, 27, 0, 16, 8, smooth=False)),  # handguard
    ('steel',   prism(300, 382, 27, 0, 6, 10)),                 # barrel
    ('black',   prism(382, 404, 27, 0, 8, 8, smooth=False)),    # muzzle brake
    ('polymer', box((295, 46, 0), (5, 14, 3))),                 # front sight post
    ('black',   box((95, -40, 0), (13, 34, 10), 10)),           # magazine
    ('black',   box((108, -82, 0), (13, 12, 10), 22)),          # magazine curve
    grip((38, -30, 0), (15, 27, 11), -22, 'polymer'),
    ('polymer', box((-50, 26, 0), (40, 14, 10))),               # buffer tube / stock front
    ('polymer', box((-120, 18, 0), (32, 26, 12))),              # stock
    ('rubber',  box((-155, 18, 0), (4, 28, 12.5))),             # butt pad
    ('black',   box((55, 50, 0), (6, 6, 6))),                   # optic mounts
    ('black',   box((115, 50, 0), (6, 6, 6))),
    ('black',   prism(35, 140, 66, 0, 11, 10)),                 # optic tube
    ('black',   prism(135, 150, 66, 0, 14, 10)),                # objective bell
    ('glass',   prism(150, 151, 66, 0, 12, 10)),                # lens
] + trigger_group(70, 'polymer', 'steel'),
    part=dict(pivot=(-6, 40, 0), geom=[
        ('gunmetal', box((0, 0, 0), (12, 4, 9))),               # charging handle
    ]), muzzle=(406, 27, 0), flash=1))


# 6: minigun - barrel cluster spins up while firing
def barrel_cluster(x0, x1, ring_r, r, n=6):
    parts = []
    for k in range(n):
        a = 2 * math.pi * k / n
        parts += prism(x0, x1, ring_r * math.cos(a), ring_r * math.sin(a), r, 8)
    return parts
W.append(dict(name='minigun', body=[
    ('gunmetal', box((25, 25, 0), (65, 25, 22))),               # receiver
    ('black',    prism(-62, -40, 25, 0, 22, 10)),               # motor
    ('gunmetal', box((40, 62, 0), (30, 3, 4))),                 # carry handle
    ('gunmetal', box((15, 54, 0), (3, 6, 4))),
    ('gunmetal', box((65, 54, 0), (3, 6, 4))),
    ('olive',    box((30, -26, 36), (30, 24, 14))),             # ammo box
    ('steel',    box((30, -4, 22), (22, 3, 2))),                # feed chute
    grip((20, -32, 0), (16, 28, 11), -14, 'polymer'),
] + trigger_group(55, 'gunmetal', 'steel'),
    part=dict(pivot=(90, 25, 0), geom=[
        ('steel',    barrel_cluster(90, 350, 13, 5)),
        ('gunmetal', prism(90, 104, 25, 0, 22, 10)),            # rear clamp
        ('gunmetal', prism(250, 262, 25, 0, 21, 10)),           # mid clamp
        ('gunmetal', prism(336, 348, 25, 0, 21, 10)),           # front clamp
    ]), muzzle=(352, 25, 0), flash=1, spin_part=True))

# 7: rocket launcher - shoulder tube
W.append(dict(name='rocket', body=[
    ('olive',  prism(-130, 250, 30, 0, 26, 12)),                # tube
    ('black',  prism(250, 272, 30, 0, 31, 12)),                 # front flare
    ('black',  prism(-150, -130, 30, 0, 31, 12)),               # rear flare
    ('black',  box((60, 64, -14), (26, 9, 6))),                 # sight
    ('glass',  box((86, 64, -14), (1, 6, 4))),
    ('olive',  box((-40, 2, 0), (30, 6, 10))),                  # shoulder pad
    grip((40, -26, 0), (15, 26, 11), -14, 'black'),
    grip((150, -22, 0), (12, 22, 10), 4, 'black'),              # front grip
] + trigger_group(70, 'black', 'steel', depth=20), part=None, muzzle=(274, 30, 0), flash=1))

# 8: grenade launcher - revolving drum turns after each shot
W.append(dict(name='grenade', body=[
    ('gunmetal', prism(90, 262, 30, 0, 16, 12)),                # barrel
    ('black',    prism(255, 264, 30, 0, 18, 12)),
    ('gunmetal', box((60, 56, 0), (40, 4, 6))),                 # top strap
    ('gunmetal', box((10, 22, 0), (24, 22, 14))),               # rear frame
    ('polymer',  box((-70, 20, 0), (50, 16, 11))),              # stock
    ('rubber',   box((-124, 20, 0), (4, 20, 12))),
    grip((10, -30, 0), (15, 26, 11), -18, 'polymer'),
    grip((180, -14, 0), (11, 20, 10), 6, 'polymer'),            # front grip
] + trigger_group(46, 'gunmetal', 'steel', depth=22),
    part=dict(pivot=(60, 22, 0), geom=[
        ('olive', prism(-30, 30, 0, 0, 34, 6, smooth=False)),   # drum
        ('black', prism(30, 32, 0, 0, 10, 8)),                  # drum axle
    ]), muzzle=(266, 30, 0), flash=1))

# 9: flamethrower - fuel tank, long nozzle
W.append(dict(name='flamethrower', body=[
    ('redtank', prism(-30, 150, -34, 0, 22, 12)),               # fuel tank
    ('steel',   prism(-38, -30, -34, 0, 16, 12)),
    ('steel',   prism(150, 158, -34, 0, 16, 12)),
    ('gunmetal', box((60, 18, 0), (70, 16, 12))),               # body
    ('steel',   prism(130, 300, 20, 0, 9, 10)),                 # nozzle pipe
    ('black',   prism(300, 322, 20, 0, 14, 10)),                # nozzle tip
    ('steel',   prism(140, 300, 2, 0, 3, 6)),                   # fuel line
    grip((40, -30, 0), (15, 26, 11), -16, 'polymer'),
    grip((210, -6, 0), (10, 20, 9), 8, 'polymer'),
] + trigger_group(75, 'gunmetal', 'steel', depth=22), part=None, muzzle=(324, 20, 0), flash=0))

# 10: sniper rifle - long barrel, big scope, bolt cycles after each shot
W.append(dict(name='sniper', body=[
    ('olive',   box((60, 22, 0), (90, 16, 12))),                # receiver / stock front
    ('olive',   box((-110, 14, 0), (70, 26, 12))),              # stock
    ('rubber',  box((-184, 14, 0), (4, 28, 12.5))),
    ('olive',   box((-80, 44, 0), (30, 6, 9))),                 # cheek rest
    ('steel',   prism(150, 520, 27, 0, 7, 10)),                 # barrel
    ('black',   prism(520, 546, 27, 0, 10, 8, smooth=False)),   # muzzle brake
    ('black',   box((50, 46, 0), (6, 6, 6))),                   # scope mounts
    ('black',   box((140, 46, 0), (6, 6, 6))),
    ('black',   prism(20, 210, 64, 0, 13, 10)),                 # scope
    ('black',   prism(210, 240, 64, 0, 18, 10)),
    ('black',   prism(0, 20, 64, 0, 16, 10)),
    ('glass',   prism(240, 241, 64, 0, 16, 10)),
    ('gunmetal', box((330, 12, 7), (60, 2, 2), -4)),            # folded bipod
    ('gunmetal', box((330, 12, -7), (60, 2, 2), -4)),
    grip((30, -30, 0), (15, 27, 11), -20, 'olive'),
    ('black',   box((95, -26, 0), (14, 20, 9))),                # magazine
] + trigger_group(62, 'olive', 'steel'),
    part=dict(pivot=(100, 30, 13), geom=[
        ('steel', box((0, 0, 6), (6, 4, 6))),                   # bolt handle
        ('steel', prism(-4, 4, 0, 14, 6, 8)),                   # bolt knob
    ]), muzzle=(548, 27, 0), flash=1))

# 11: railgun - ceramic body, glowing rails, coils
W.append(dict(name='railgun', body=[
    ('ceramic',  box((85, 22, 0), (115, 22, 16))),              # body
    ('ceramic',  box((-70, 18, 0), (40, 18, 12))),              # stock
    ('steel',    box((240, 38, 0), (120, 4, 6))),               # top rail
    ('steel',    box((240, 10, 0), (120, 4, 6))),               # bottom rail
    ('glowcyan', box((240, 24, 0), (118, 6, 3))),               # energy core
    ('gunmetal', prism(160, 176, 24, 0, 26, 10)),               # coils
    ('gunmetal', prism(230, 246, 24, 0, 26, 10)),
    ('gunmetal', prism(300, 316, 24, 0, 26, 10)),
    ('glowcyan', box((40, 50, 0), (40, 4, 8))),                 # charge display
    grip((30, -28, 0), (15, 26, 11), -18, 'polymer'),
] + trigger_group(62, 'gunmetal', 'steel', depth=22), part=None, muzzle=(362, 24, 0), flash=1))

# 12: double barrel - side-by-side barrels break open after each shot
W.append(dict(name='doublebarrel', body=[
    ('blued',    box((70, 22, 0), (34, 16, 15))),               # receiver
    ('wood',     box((-30, 18, 0), (40, 13, 11), -5)),          # wrist
    ('wood',     box((-120, 6, 0), (55, 26, 13), -9)),          # stock
    ('rubber',   box((-178, -2, 0), (4, 30, 14), -9)),
    grip((20, -24, 0), (15, 24, 11), -26, 'wood'),
] + trigger_group(60, 'blued', 'steel', depth=20),
    part=dict(pivot=(100, 14, 0), geom=[
        ('blued', prism(0, 300, 16, 9, 9, 10)),                 # barrels
        ('blued', prism(0, 300, 16, -9, 9, 10)),
        ('steel', box((150, 26, 0), (150, 2, 3))),              # rib
        ('steel', box((296, 30, 0), (2, 2, 2))),                # bead
        ('wood',  box((90, 2, 0), (70, 9, 14))),                # forend
    ]), muzzle=(402, 30, 0), flash=2))

# 13: plasma rifle - purple body, glowing green emitter
W.append(dict(name='plasma', body=[
    ('purple',    box((80, 20, 0), (100, 20, 14))),             # body
    ('purple',    box((-50, 14, 0), (30, 14, 11))),             # stock
    ('glowgreen', box((70, 46, 0), (40, 6, 7))),                # energy cell
    ('gunmetal',  prism(180, 242, 20, 0, 14, 10)),              # emitter
    ('glowgreen', prism(195, 203, 20, 0, 17, 10)),              # glow rings
    ('glowgreen', prism(215, 223, 20, 0, 17, 10)),
    ('glowgreen', prism(242, 244, 20, 0, 9, 10)),               # emitter face
    grip((30, -28, 0), (15, 26, 11), -18, 'polymer'),
] + trigger_group(62, 'purple', 'steel', depth=22), part=None, muzzle=(246, 20, 0), flash=1))


# 14: M4A1-style carbine - flat-top upper, quad-rail handguard, collapsible stock, 30-round mag
def m4_parts():
    return [
        ('polymer',  box((75, 27, 0), (75, 13, 11))),               # upper receiver
        ('polymer',  box((70, 6, 0), (50, 8, 10))),                 # lower receiver
        ('gunmetal', box((75, 43, 0), (75, 3, 7))),                 # top rail
        ('black',    box((60, 27, 11.2), (16, 6, 0.6))),            # ejection port
        ('black',    prism(150, 288, 26, 0, 15, 8, smooth=False)),  # quad-rail handguard
        ('gunmetal', box((219, 43, 0), (69, 3, 6))),                # handguard top rail
        ('steel',    prism(288, 352, 26, 0, 6, 10)),                # barrel
        ('black',    prism(352, 374, 26, 0, 8, 6, smooth=False)),   # flash hider
        ('black',    box((276, 50, 0), (5, 14, 3))),                # front sight post
        ('black',    box((276, 36, 0), (8, 4, 6))),                 # front sight base
        ('black',    box((16, 54, 0), (7, 8, 5))),                  # rear flip sight
        ('black',    box((92, -36, 0), (12, 30, 10), 8)),           # magazine
        ('black',    box((104, -72, 0), (12, 10, 10), 18)),         # magazine curve
        grip((30, -28, 0), (15, 26, 11), -22, 'polymer'),
        ('black',    prism(-110, 0, 26, 0, 9, 10)),                 # buffer tube
        ('polymer',  box((-112, 22, 0), (34, 18, 12))),             # collapsible stock
        ('rubber',   box((-149, 22, 0), (4, 20, 12.5))),            # butt pad
    ] + trigger_group(62, 'polymer', 'steel')
W.append(dict(name='m4a1', body=m4_parts(),
    part=dict(pivot=(-4, 40, 0), geom=[
        ('gunmetal', box((0, 0, 0), (10, 3, 8))),                   # charging handle
    ]), muzzle=(376, 26, 0), flash=1))

# ------------------------------------------------------------------ emit helpers
def nb(v): return int(round(max(-1, min(1, v)) * 127)) & 0xFF

def to_fist(tris, offset=(0, 0, 0)):
    """Logical -> fist frame: subtract offset, scale, flip Y, reverse winding."""
    out = []
    for t in fix_winding(tris):
        nt = []
        for p, n in t:
            q = ((p[0] - offset[0]) * SCALE, -(p[1] - offset[1]) * SCALE, (p[2] - offset[2]) * SCALE)
            nn = (n[0], -n[1], n[2])
            nt.append((q, nn))
        out.append([nt[0], nt[2], nt[1]])
    return out

def emit_mesh(name, tris):
    verts, gfx, batch, cur = [], [], [], {}
    def flush():
        if not batch: return
        start = len(verts)
        for k, _ in sorted(cur.items(), key=lambda kv: kv[1]): verts.append(k)
        gfx.append(f"    gsSPVertex({name}_vtx + {start}, {len(cur)}, 0),")
        for i in range(0, len(batch), 2):
            if i + 1 < len(batch):
                a, b = batch[i], batch[i + 1]
                gfx.append(f"    gsSP2Triangles({a[0]}, {a[1]}, {a[2]}, 0x0, {b[0]}, {b[1]}, {b[2]}, 0x0),")
            else:
                a = batch[i]; gfx.append(f"    gsSP1Triangle({a[0]}, {a[1]}, {a[2]}, 0x0),")
        batch.clear(); cur.clear()
    for t in tris:
        keys = [(tuple(int(round(c)) for c in p), tuple(nb(c) for c in n)) for p, n in t]
        if len(set(k[0] for k in keys)) < 3:
            continue  # degenerate after rounding
        new = [k for k in dict.fromkeys(keys) if k not in cur]
        if len(cur) + len(new) > 16: flush()
        for k in keys:
            if k not in cur: cur[k] = len(cur)
        batch.append(tuple(cur[k] for k in keys))
    flush()
    vtx = [f"static const Vtx {name}_vtx[] = {{"]
    for p, n in verts:
        vtx.append(f"    {{{{{{ {p[0]}, {p[1]}, {p[2]} }}, 0, {{ 0, 0 }}, {{ 0x{n[0]:02x}, 0x{n[1]:02x}, 0x{n[2]:02x}, 0xff }}}}}},")
    vtx.append("};\n")
    return "\n".join(vtx), gfx

out = ["// Mario's weapons - generated by gen_weapons.py. Do not edit by hand.\n"]
for c, (r, g, b) in COLORS.items():
    ar, ag, ab = (r, g, b) if c in GLOW else (r // 2, g // 2, b // 2)
    out.append(f"static const Lights1 weapon_{c}_lights = gdSPDefLights1(0x{ar:02x}, 0x{ag:02x}, 0x{ab:02x}, 0x{r:02x}, 0x{g:02x}, 0x{b:02x}, 0x28, 0x28, 0x28);\n")

def emit_model(dlname, parts, offset=(0, 0, 0)):
    groups = {}
    for color, tris in parts:
        groups.setdefault(color, []).extend(tris)
    body = ["    gsDPPipeSync(),", "    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),",
            "    gsDPSetCombineMode(G_CC_SHADEFADEA, G_CC_SHADEFADEA),"]
    for color, tris in groups.items():
        vtx, gfx = emit_mesh(f"{dlname}_{color}", to_fist(tris, offset))
        out.append(vtx)
        body += [f"    gsSPLight(&weapon_{color}_lights.l, 1),", f"    gsSPLight(&weapon_{color}_lights.a, 2),"] + gfx
    out.append(f"const Gfx {dlname}[] = {{\n" + "\n".join(body) + "\n    gsSPEndDisplayList(),\n};\n")

for w in W:
    emit_model(f"mario_weapon_{w['name']}", w['body'])
    if w['part']:
        emit_model(f"mario_weapon_{w['name']}_part", w['part']['geom'], w['part']['pivot'])

# ---- unlit effects (flat primitive color, double sided)
def unlit_dl(name, verts, tris, rgb):
    out.append(f"static const Vtx {name}_vtx[] = {{\n" + "\n".join(
        f"    {{{{{{ {int(x)}, {int(y)}, {int(z)} }}, 0, {{ 0, 0 }}, {{ 0xff, 0xff, 0xff, 0xff }}}}}}," for x, y, z in verts) + "\n};\n")
    lines = []
    for i in range(0, len(tris), 2):
        a = tris[i]
        if i + 1 < len(tris):
            b = tris[i + 1]; lines.append(f"    gsSP2Triangles({a[0]}, {a[1]}, {a[2]}, 0x0, {b[0]}, {b[1]}, {b[2]}, 0x0),")
        else:
            lines.append(f"    gsSP1Triangle({a[0]}, {a[1]}, {a[2]}, 0x0),")
    out.append(f"const Gfx {name}[] = {{\n    gsDPPipeSync(),\n    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),\n"
               f"    gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE),\n    gsDPSetPrimColor(0, 0, {rgb[0]}, {rgb[1]}, {rgb[2]}, 255),\n"
               f"    gsSPClearGeometryMode(G_CULL_BACK),\n    gsSPVertex({name}_vtx, {len(verts)}, 0),\n" + "\n".join(lines) +
               "\n    gsDPPipeSync(),\n    gsSPSetGeometryMode(G_CULL_BACK),\n    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),\n    gsSPEndDisplayList(),\n};\n")

def diamond(L, R):  # elongated octahedron along +Z (object forward)
    v = [(0, 0, L), (0, 0, -L), (R, 0, 0), (-R, 0, 0), (0, R, 0), (0, -R, 0)]
    t = [(0, 2, 4), (0, 4, 3), (0, 3, 5), (0, 5, 2), (1, 4, 2), (1, 3, 4), (1, 5, 3), (1, 2, 5)]
    return v, t

v, t = diamond(40, 9);  unlit_dl("zapper_shot_dl", v, t, (255, 245, 120))   # light gun bolt
v, t = diamond(34, 12); unlit_dl("plasma_bolt_dl", v, t, (120, 255, 140))   # plasma bolt
v, t = diamond(60, 5);  unlit_dl("rail_beam_dl", v, t, (110, 240, 255))     # railgun beam segment

def to_object_space(tris, scale=1.0, offset=(0, 0, 0)):
    # authored along +X; swap X and Z so it points along the object's forward axis (+Z)
    return fix_winding([[(((p[2]) * scale + offset[0], p[1] * scale + offset[1], p[0] * scale + offset[2]),
                           (n[2], n[1], n[0])) for p, n in t] for t in tris])

def emit_object_model(dlname, parts, scale=1.0, offset=(0, 0, 0), swap=True):
    groups = {}
    for color, tris in parts:
        if swap:
            tris = to_object_space(tris, scale, offset)
        else:
            tris = fix_winding([[((p[0] * scale + offset[0], p[1] * scale + offset[1], p[2] * scale + offset[2]), n) for p, n in t] for t in tris])
        groups.setdefault(color, []).extend(tris)
    body = ["    gsDPPipeSync(),", "    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),",
            "    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),"]
    for color, tris in groups.items():
        vtx, gfx = emit_mesh(f"{dlname}_{color}", tris)
        out.append(vtx)
        body += [f"    gsSPLight(&weapon_{color}_lights.l, 1),", f"    gsSPLight(&weapon_{color}_lights.a, 2),"] + gfx
    out.append(f"const Gfx {dlname}[] = {{\n" + "\n".join(body) + "\n    gsSPEndDisplayList(),\n};\n")

fin = lambda a: ('black', box((-22 + 4, 0, 0), (8, 1.5, 12), 0)) if a == 0 else ('black', box((-22 + 4, 0, 0), (8, 12, 1.5), 0))
emit_object_model("rocket_projectile_dl", [
    ('olive',  prism(-26, 22, 0, 0, 6, 8)),
    ('redtank', prism(22, 36, 0, 0, 6, 8, r1=0.5)),
    ('black',  prism(-30, -26, 0, 0, 4.5, 8)),
    fin(0), fin(1),
])
def ball(r, color, lat=4, lon=8):
    tris = []
    for i in range(lat):
        for j in range(lon):
            def P(a, b):
                th = math.pi * a / lat; ph = 2 * math.pi * b / lon
                u = (math.cos(th), math.sin(th) * math.cos(ph), math.sin(th) * math.sin(ph))
                return (tuple(r * c for c in u), u)
            A, B, C, D = P(i, j), P(i, j + 1), P(i + 1, j), P(i + 1, j + 1)
            if i != 0: tris.append([A, C, B])
            if i != lat - 1: tris.append([B, C, D])
    return (color, tris)
emit_object_model("grenade_projectile_dl", [ball(10, 'olive'), ('black', prism(-3, 3, 0, 0, 10.5, 8))])
v, t = diamond(28, 3);  unlit_dl("bullet_tracer_dl", v, t, (255, 200, 70))   # bullet tracer

# muzzle flash: two crossed star shapes pointing along fist +X (in fist frame, already flipped)
fv, ft = [], []
for plane in ('y', 'z'):
    base = len(fv)
    pts = [(0, 0), (70, 0), (20, 22), (20, -22), (40, 12), (40, -12)]
    for x, s in pts:
        fv.append((x * SCALE, s * SCALE, 0) if plane == 'y' else (x * SCALE, 0, s * SCALE))
    ft += [(base + 0, base + 2, base + 4), (base + 0, base + 4, base + 1), (base + 0, base + 1, base + 5),
           (base + 0, base + 5, base + 3)]
unlit_dl("mario_weapon_flash", fv, ft, (255, 210, 90))


# ------------------------------------------------------------------ desert trooper (original enemy design)
COLORS_T = {'khaki': (0xB4, 0x9C, 0x6C), 'armor': (0x6E, 0x6A, 0x48), 'mask': (0x34, 0x32, 0x30),
            'boot': (0x3A, 0x2A, 0x1C), 'visor': (0xFF, 0x30, 0x20), 'glove': (0x2A, 0x2A, 0x2A)}
for c, (r, g, b) in COLORS_T.items():
    ar, ag, ab = (r, g, b) if c == 'visor' else (r // 2, g // 2, b // 2)
    out.append(f"static const Lights1 weapon_{c}_lights = gdSPDefLights1(0x{ar:02x}, 0x{ag:02x}, 0x{ab:02x}, 0x{r:02x}, 0x{g:02x}, 0x{b:02x}, 0x28, 0x28, 0x28);\n")

def ybox(center, half):  # plain axis box in object space
    return box(center, half)

TROOPER_RIFLE_SCALE = 0.33
TROOPER_RIFLE_POS = (-14, 112, 34)  # grip position in the trooper's hands
emit_object_model("trooper_body_dl", [
    ('khaki', ybox((0, 112, 0), (26, 30, 15))),          # torso
    ('armor', ybox((0, 118, 1), (29, 22, 19))),          # plate carrier
    ('armor', ybox((-17, 124, 20), (6, 9, 2))),          # mag pouches
    ('armor', ybox((0, 124, 20), (6, 9, 2))),
    ('armor', ybox((17, 124, 20), (6, 9, 2))),
    ('boot',  ybox((0, 84, 0), (27, 4, 16))),            # belt
    ('mask',  ybox((0, 156, 2), (15, 15, 15))),          # balaclava head
    ('armor', ybox((0, 173, 1), (19, 7, 20))),           # helmet
    ('armor', ybox((0, 167, 3), (21, 3, 22))),           # helmet rim
    ('visor', ybox((0, 157, 17), (12, 4, 1.5))),         # glowing visor
    ('khaki', ybox((-34, 126, 6), (8, 14, 9))),          # right upper arm
    ('khaki', ybox((34, 126, 6), (8, 14, 9))),           # left upper arm
    ('khaki', ybox((-24, 112, 26), (7, 7, 16))),         # right forearm (to grip)
    ('khaki', ybox((18, 116, 38), (7, 7, 24))),          # left forearm (to handguard)
    ('glove', ybox((-14, 110, 40), (8, 8, 8))),          # right hand
    ('glove', ybox((6, 114, 64), (8, 8, 8))),            # left hand
], swap=False)
emit_object_model("trooper_rifle_dl", m4_parts(), scale=TROOPER_RIFLE_SCALE,
                  offset=(TROOPER_RIFLE_POS[0], TROOPER_RIFLE_POS[1], TROOPER_RIFLE_POS[2]))
emit_object_model("trooper_leg_dl", [
    ('khaki', ybox((0, -40, 0), (10, 40, 11))),          # leg (hangs from the hip pivot)
    ('boot',  ybox((0, -76, 5), (11, 7, 17))),           # boot
], swap=False)
TROOPER_MUZZLE = (TROOPER_RIFLE_POS[0], TROOPER_RIFLE_POS[1] + 26 * TROOPER_RIFLE_SCALE, TROOPER_RIFLE_POS[2] + 376 * TROOPER_RIFLE_SCALE)

# ------------------------------------------------------------------ jetpack (Mario's torso frame: +X up the spine, +Y forward, Z sideways)
JET_NOZZLES = [(-34, -108, 32), (-34, -108, -32)]
emit_object_model("mario_jetpack_dl", [
    ('gunmetal', box((35, -82, 0), (45, 6, 44))),                 # back plate
    ('steel',    prism(-10, 95, -108, 32, 24, 12)),               # tanks
    ('steel',    prism(-10, 95, -108, -32, 24, 12)),
    ('redtank',  prism(95, 108, -108, 32, 22, 12, r1=12)),        # tank domes
    ('redtank',  prism(95, 108, -108, -32, 22, 12, r1=12)),
    ('redtank',  prism(40, 52, -108, 32, 25, 12)),                # stripes
    ('redtank',  prism(40, 52, -108, -32, 25, 12)),
    ('black',    prism(-34, -10, -108, 32, 18, 12, r1=13)),       # nozzle bells
    ('black',    prism(-34, -10, -108, -32, 18, 12, r1=13)),
    ('gunmetal', box((-2, -100, 0), (6, 10, 40))),                # cross brace
], swap=False)

def unlit_multi(name, groups):
    """groups: [(rgb, tris)] - flat unlit colors, double sided."""
    vtx, body = [], []
    for rgb, tris in groups:
        body.append(f"    gsDPSetPrimColor(0, 0, {rgb[0]}, {rgb[1]}, {rgb[2]}, 255),")
        for i in range(0, len(tris), 5):
            chunk = tris[i:i + 5]
            body.append(f"    gsSPVertex({name}_vtx + {len(vtx)}, {len(chunk) * 3}, 0),")
            for j, t in enumerate(chunk):
                for p_, _ in t:
                    vtx.append(p_)
                body.append(f"    gsSP1Triangle({j * 3}, {j * 3 + 1}, {j * 3 + 2}, 0x0),")
    out.append(f"static const Vtx {name}_vtx[] = {{\n" + "\n".join(
        f"    {{{{{{ {int(round(x))}, {int(round(y))}, {int(round(z))} }}, 0, {{ 0, 0 }}, {{ 0xff, 0xff, 0xff, 0xff }}}}}}," for x, y, z in vtx) + "\n};\n")
    out.append(f"const Gfx {name}[] = {{\n    gsDPPipeSync(),\n    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF),\n"
               "    gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE),\n    gsSPClearGeometryMode(G_CULL_BACK),\n" + "\n".join(body) +
               "\n    gsDPPipeSync(),\n    gsSPSetGeometryMode(G_CULL_BACK),\n    gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),\n    gsSPEndDisplayList(),\n};\n")

# exhaust flame: cone pointing down the spine (-X) from the nozzle; outer orange, inner yellow-white
unlit_multi("mario_jetpack_flame_dl", [
    ((255, 120, 20), prism(-120, 0, 0, 0, 1, 10, r1=17)),
    ((255, 235, 140), prism(-75, 2, 0, 0, 1, 8, r1=10)),
])

open(os.path.join(HERE, 'weapons.inc.c'), 'w').write("\n".join(out))

# ------------------------------------------------------------------ geo layouts
def fist_pos(p):
    return tuple(int(round(v)) for v in (p[0] * SCALE, -p[1] * SCALE, p[2] * SCALE))

geo = ["// Mario's weapons - generated by gen_weapons.py. Do not edit by hand.\n"]
for layer_name, layer in (('opaque', 'LAYER_OPAQUE'), ('transparent', 'LAYER_TRANSPARENT')):
    g = [f"const GeoLayout mario_weapon_geo_{layer_name}[] = {{",
         "    GEO_ASM(0, geo_weapon_recoil),",
         "    GEO_TRANSLATE_ROTATE(0, 0, 0, 0, 0, 0, 0),",
         "    GEO_OPEN_NODE(),",
         "        GEO_ASM(0, geo_weapon_draw_scale),",
         "        GEO_SCALE(0x00, 65536),",
         "        GEO_OPEN_NODE(),",
         f"            GEO_SWITCH_CASE({len(W)}, geo_switch_weapon),",
         "            GEO_OPEN_NODE(),"]
    for i, w in enumerate(W):
        g += ["                GEO_NODE_START(),", "                GEO_OPEN_NODE(),",
              f"                    GEO_DISPLAY_LIST({layer}, mario_weapon_{w['name']}),"]
        if w['part']:
            px, py, pz = fist_pos(w['part']['pivot'])
            g += [f"                    GEO_TRANSLATE_ROTATE(0, {px}, {py}, {pz}, 0, 0, 0),",
                  "                    GEO_OPEN_NODE(),",
                  f"                        GEO_ASM({i}, geo_weapon_moving_part),",
                  "                        GEO_TRANSLATE_ROTATE(0, 0, 0, 0, 0, 0, 0),",
                  "                        GEO_OPEN_NODE(),",
                  f"                            GEO_DISPLAY_LIST({layer}, mario_weapon_{w['name']}_part),",
                  "                        GEO_CLOSE_NODE(),",
                  "                    GEO_CLOSE_NODE(),"]
        if w['flash']:
            mx, my, mz = fist_pos(w['muzzle'])
            g += [f"                    GEO_TRANSLATE_ROTATE(0, {mx}, {my}, {mz}, 0, 0, 0),",
                  "                    GEO_OPEN_NODE(),",
                  f"                        GEO_ASM({i}, geo_weapon_muzzle_flash),",
                  "                        GEO_SCALE(0x00, 0),",
                  "                        GEO_OPEN_NODE(),",
                  "                            GEO_DISPLAY_LIST(LAYER_OPAQUE, mario_weapon_flash),",
                  "                        GEO_CLOSE_NODE(),",
                  "                    GEO_CLOSE_NODE(),"]
        g += ["                GEO_CLOSE_NODE(),"]
    g += ["            GEO_CLOSE_NODE(),", "        GEO_CLOSE_NODE(),", "    GEO_CLOSE_NODE(),", "    GEO_RETURN(),", "};\n"]
    geo += g

for name, dl in (('zapper_shot_geo', 'zapper_shot_dl'), ('bullet_tracer_geo', 'bullet_tracer_dl'),
                 ('plasma_bolt_geo', 'plasma_bolt_dl'), ('rail_beam_geo', 'rail_beam_dl'),
                 ('rocket_projectile_geo', 'rocket_projectile_dl'), ('grenade_projectile_geo', 'grenade_projectile_dl')):
    geo += [f"const GeoLayout {name}[] = {{", "    GEO_CULLING_RADIUS(100),", "    GEO_OPEN_NODE(),",
            f"        GEO_DISPLAY_LIST(LAYER_OPAQUE, {dl}),", "    GEO_CLOSE_NODE(),", "    GEO_END(),", "};\n"]

mx, my, mz = (int(round(v)) for v in TROOPER_MUZZLE)
geo += ["const GeoLayout desert_trooper_geo[] = {",
        "    GEO_SHADOW(SHADOW_CIRCLE_4_VERTS, 0x9B, 90),",
        "    GEO_OPEN_NODE(),",
        "        GEO_ASM(0, geo_trooper_body),",
        "        GEO_TRANSLATE_ROTATE(0, 0, 0, 0, 0, 0, 0),",
        "        GEO_OPEN_NODE(),",
        "            GEO_DISPLAY_LIST(LAYER_OPAQUE, trooper_body_dl),",
        "            GEO_DISPLAY_LIST(LAYER_OPAQUE, trooper_rifle_dl),",
        "            GEO_TRANSLATE_ROTATE(0, 14, 82, 0, 0, 0, 0),",
        "            GEO_OPEN_NODE(),",
        "                GEO_ASM(1, geo_trooper_leg),",
        "                GEO_TRANSLATE_ROTATE(0, 0, 0, 0, 0, 0, 0),",
        "                GEO_OPEN_NODE(),",
        "                    GEO_DISPLAY_LIST(LAYER_OPAQUE, trooper_leg_dl),",
        "                GEO_CLOSE_NODE(),",
        "            GEO_CLOSE_NODE(),",
        "            GEO_TRANSLATE_ROTATE(0, -14, 82, 0, 0, 0, 0),",
        "            GEO_OPEN_NODE(),",
        "                GEO_ASM(2, geo_trooper_leg),",
        "                GEO_TRANSLATE_ROTATE(0, 0, 0, 0, 0, 0, 0),",
        "                GEO_OPEN_NODE(),",
        "                    GEO_DISPLAY_LIST(LAYER_OPAQUE, trooper_leg_dl),",
        "                GEO_CLOSE_NODE(),",
        "            GEO_CLOSE_NODE(),",
        f"            GEO_TRANSLATE_ROTATE(0, {mx}, {my}, {mz}, 0, -90, 0),",
        "            GEO_OPEN_NODE(),",
        "                GEO_ASM(0, geo_trooper_flash),",
        "                GEO_SCALE(0x00, 0),",
        "                GEO_OPEN_NODE(),",
        "                    GEO_DISPLAY_LIST(LAYER_OPAQUE, mario_weapon_flash),",
        "                GEO_CLOSE_NODE(),",
        "            GEO_CLOSE_NODE(),",
        "        GEO_CLOSE_NODE(),",
        "    GEO_CLOSE_NODE(),",
        "    GEO_END(),",
        "};\n"]

for layer_name, layer in (('opaque', 'LAYER_OPAQUE'), ('transparent', 'LAYER_TRANSPARENT')):
    geo += [f"const GeoLayout mario_jetpack_geo_{layer_name}[] = {{",
            f"    GEO_DISPLAY_LIST({layer}, mario_jetpack_dl),"]
    for (nx, ny, nz) in JET_NOZZLES:
        geo += [f"    GEO_TRANSLATE_ROTATE(0, {nx}, {ny}, {nz}, 0, 0, 0),", "    GEO_OPEN_NODE(),",
                "        GEO_ASM(0, geo_jetpack_flame),", "        GEO_SCALE(0x00, 0),", "        GEO_OPEN_NODE(),",
                "            GEO_DISPLAY_LIST(LAYER_OPAQUE, mario_jetpack_flame_dl),", "        GEO_CLOSE_NODE(),", "    GEO_CLOSE_NODE(),"]
    geo += ["    GEO_RETURN(),", "};\n"]
open(os.path.join(HERE, 'weapons_geo.inc.c'), 'w').write("\n".join(geo))

# externs for group0.h
externs = [f"extern const Gfx mario_weapon_{w['name']}[];" for w in W]
externs += [f"extern const Gfx mario_weapon_{w['name']}_part[];" for w in W if w['part']]
externs += ["extern const Gfx mario_weapon_flash[];", "extern const Gfx zapper_shot_dl[];", "extern const Gfx bullet_tracer_dl[];",
            "extern const Gfx plasma_bolt_dl[];", "extern const Gfx rail_beam_dl[];", "extern const Gfx rocket_projectile_dl[];",
            "extern const Gfx grenade_projectile_dl[];", "extern const GeoLayout plasma_bolt_geo[];", "extern const GeoLayout rail_beam_geo[];",
            "extern const GeoLayout rocket_projectile_geo[];", "extern const GeoLayout grenade_projectile_geo[];",
            "extern const GeoLayout mario_weapon_geo_opaque[];", "extern const GeoLayout mario_weapon_geo_transparent[];",
            "extern const GeoLayout zapper_shot_geo[];", "extern const GeoLayout bullet_tracer_geo[];",
            "extern const Gfx trooper_body_dl[];", "extern const Gfx trooper_rifle_dl[];", "extern const Gfx trooper_leg_dl[];",
            "extern const GeoLayout desert_trooper_geo[];",
            "extern const Gfx mario_jetpack_dl[];", "extern const Gfx mario_jetpack_flame_dl[];",
            "extern const GeoLayout mario_jetpack_geo_opaque[];", "extern const GeoLayout mario_jetpack_geo_transparent[];"]
open(os.path.join(HERE, 'weapons_externs.h'), 'w').write("// generated by gen_weapons.py\n" + "\n".join(externs) + "\n")
print(f"wrote weapons.inc.c, weapons_geo.inc.c, weapons_externs.h ({len(W)} weapons, scale {SCALE})")
