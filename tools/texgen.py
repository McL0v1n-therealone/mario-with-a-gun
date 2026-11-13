#!/usr/bin/env python3
"""Procedural 32x32 RGBA16 textures in a late-90s console style (original, tileable).

Each texture is a mostly-neutral detail map; models tint it with their light / vertex colour
(G_CC_MODULATERGB), the same way many N64 games reused a few small greyscale detail textures.
Used by actors/mario/gen_weapons.py and levels/outpost/gen_outpost.py.
"""
import math, random

N = 32


def _noise(seed, cells):
    """Tileable value noise sampled on an N x N grid."""
    r = random.Random(seed)
    grid = [[r.random() for _ in range(cells)] for _ in range(cells)]
    out = [[0.0] * N for _ in range(N)]
    for y in range(N):
        for x in range(N):
            fx, fy = x * cells / N, y * cells / N
            x0, y0 = int(fx), int(fy)
            tx, ty = fx - x0, fy - y0
            tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
            a = grid[y0 % cells][x0 % cells]; b = grid[y0 % cells][(x0 + 1) % cells]
            c = grid[(y0 + 1) % cells][x0 % cells]; d = grid[(y0 + 1) % cells][(x0 + 1) % cells]
            out[y][x] = (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty
    return out


def _grey(f):
    return lambda x, y: (f(x, y),) * 3


def grain():          # wood grain running along U
    n1, n2 = _noise(1, 4), _noise(2, 16)
    return _grey(lambda x, y: 0.72 + 0.22 * (0.5 + 0.5 * math.sin((y + n1[y][x] * 6) * 1.1)) + 0.08 * n2[y][x])


def stipple():        # speckled polymer / rubber
    n = _noise(3, 32); n2 = _noise(4, 8)
    return _grey(lambda x, y: 0.80 + 0.14 * n[y][x] + 0.06 * n2[y][x])


def brushed():        # brushed / parkerized metal: fine streaks along U
    r = random.Random(5); rows = [r.random() for _ in range(N)]
    n = _noise(6, 8)
    return _grey(lambda x, y: 0.78 + 0.16 * rows[y] + 0.06 * n[y][x])


def fabric():         # woven cloth
    n = _noise(7, 16)
    return _grey(lambda x, y: 0.74 + 0.12 * ((x + y) % 2) + 0.10 * ((x // 2 + y // 2) % 2) * 0.5 + 0.08 * n[y][x])


def sand():           # wind ripples + grain
    n, n2 = _noise(8, 4), _noise(9, 32)
    return _grey(lambda x, y: 0.80 + 0.10 * math.sin((y + x * 0.25 + n[y][x] * 8) * 0.8) + 0.10 * n2[y][x])


def bricks():         # sandstone blocks: 4 rows, staggered, darker mortar
    n, n2 = _noise(10, 8), _noise(11, 32)
    def f(x, y):
        row = y // 8
        bx = (x + (8 if row % 2 else 0)) % 16
        mortar = (y % 8 == 0) or (bx == 0)
        base = 0.62 if mortar else 0.88 + 0.08 * n[y][x] - 0.05 * ((row * 7 + (x + (8 if row % 2 else 0)) // 16) % 3) / 2
        return base + 0.05 * n2[y][x]
    return _grey(f)


def planks():         # crate: frame, horizontal planks, diagonal brace
    n = _noise(12, 16)
    def f(x, y):
        frame = x < 3 or x > 28 or y < 3 or y > 28
        brace = abs(x - y) < 3
        gap = (y % 8 == 0) and not frame
        v = 0.70 if frame or brace else 0.84
        if gap:
            v = 0.50
        return v + 0.10 * n[y][x] + 0.05 * math.sin(y * 2.3 + n[y][x] * 4)
    return _grey(f)


def stone():          # large stone blocks
    n, n2 = _noise(13, 6), _noise(14, 32)
    def f(x, y):
        edge = (y % 16 == 0) or ((x + (8 if (y // 16) % 2 else 0)) % 16 == 0)
        return (0.60 if edge else 0.86 + 0.08 * n[y][x]) + 0.06 * n2[y][x]
    return _grey(f)


def doorplanks():     # vertical boards with iron bands
    n = _noise(15, 16)
    def f(x, y):
        if y in (6, 7, 24, 25):
            return 0.45 + 0.05 * n[y][x]
        return (0.55 if x % 8 == 0 else 0.86) + 0.10 * n[y][x]
    return _grey(f)


TEXTURES = {
    'grain': grain, 'stipple': stipple, 'brushed': brushed, 'fabric': fabric, 'sand': sand,
    'bricks': bricks, 'planks': planks, 'stone': stone, 'doorplanks': doorplanks,
}


def rgba16_bytes(fn):
    px = fn()
    out = []
    for y in range(N):
        for x in range(N):
            r, g, b = (max(0.0, min(1.0, c)) for c in px(x, y))
            v = (int(r * 31) << 11) | (int(g * 31) << 6) | (int(b * 31) << 1) | 1
            out += [v >> 8, v & 0xFF]
    return out


def c_texture(name, kind):
    data = rgba16_bytes(TEXTURES[kind])
    lines = [f"ALIGNED8 static const Texture {name}[] = {{"]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    lines.append("};\n")
    return "\n".join(lines)


def uv_planar(tri, normal, units_per_texel):
    """Box-style planar mapping: project on the two axes the face is most aligned with.
    Returns (u, v) per vertex in s10.5 fixed point, kept small so they never overflow."""
    ax = max(range(3), key=lambda i: abs(normal[i]))
    a, b = [(1, 2), (0, 2), (0, 1)][ax]
    us = [p[a] / units_per_texel for p in tri]
    vs = [-p[b] / units_per_texel for p in tri]
    # shift into a small range (texture repeats every 32 texels)
    du = math.floor(min(us) / 32) * 32
    dv = math.floor(min(vs) / 32) * 32
    return [(int(round((u - du) * 32)), int(round((v - dv) * 32))) for u, v in zip(us, vs)]
