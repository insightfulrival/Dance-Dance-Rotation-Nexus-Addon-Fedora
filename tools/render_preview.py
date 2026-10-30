#!/usr/bin/env python3
"""Rasterize ImGui draw data written by tests/render_preview.cpp into a PNG (visual check only).

Bundled images come from assets/images. Ability icons are drawn as gray placeholders unless an
icon cache directory with <asset>.png files is given.
"""
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def read_preview(path):
    data = memoryview(Path(path).read_bytes())
    pos = 0

    def take(fmt):
        nonlocal pos
        values = struct.unpack_from(fmt, data, pos)
        pos += struct.calcsize(fmt)
        return values

    (count,) = take('<I')
    names = []
    for _ in range(count):
        (n,) = take('<I')
        names.append(bytes(data[pos:pos + n]).decode())
        pos += n
    fw, fh = take('<ii')
    font = np.frombuffer(data[pos:pos + fw * fh * 4], dtype=np.uint8).reshape(fh, fw, 4).astype(np.float32) / 255
    pos += fw * fh * 4
    (lists,) = take('<I')
    draw_lists = []
    for _ in range(lists):
        (vc,) = take('<I')
        verts = np.frombuffer(data[pos:pos + vc * 20], dtype=np.dtype([('pos', '<f4', 2), ('uv', '<f4', 2), ('col', '<u4')]))
        pos += vc * 20
        (ic,) = take('<I')
        idx = np.frombuffer(data[pos:pos + ic * 2], dtype='<u2')
        pos += ic * 2
        (cc,) = take('<I')
        cmds = [take('<4fQIII') for _ in range(cc)]
        draw_lists.append((verts, idx, cmds))
    return names, font, draw_lists


def load_texture(name, root, icon_cache):
    if name.startswith('DDR_IMG_'):
        path = root / 'assets/images' / name[len('DDR_IMG_'):]
    elif name.startswith('DDR_ASSET_') and icon_cache:
        path = Path(icon_cache) / (name[len('DDR_ASSET_'):] + '.png')
    else:
        path = None
    if path and path.exists():
        return np.asarray(Image.open(path).convert('RGBA'), dtype=np.float32) / 255
    placeholder = np.full((8, 8, 4), 0.55, dtype=np.float32)
    placeholder[..., 3] = 1
    return placeholder


def rasterize(names, font, draw_lists, size, root, icon_cache):
    w, h = size
    canvas = np.zeros((h, w, 3), dtype=np.float32)
    canvas[:] = (0.18, 0.22, 0.26)  # stand-in for the game scene
    textures = {0xF0F0: font}
    for i, name in enumerate(names):
        textures[i + 1] = load_texture(name, root, icon_cache)
    for verts, idx, cmds in draw_lists:
        for x0, y0, x1, y1, tex, offset, count, voffset in cmds:
            texture = textures.get(tex, font)
            th, tw = texture.shape[:2]
            cx0, cy0, cx1, cy1 = max(0, int(x0)), max(0, int(y0)), min(w, int(np.ceil(x1))), min(h, int(np.ceil(y1)))
            if cx0 >= cx1 or cy0 >= cy1:
                continue
            for t in range(offset, offset + count, 3):
                tri = verts[idx[t:t + 3].astype(np.int64) + voffset]
                p = tri['pos'].astype(np.float64)
                bx0, by0 = max(cx0, int(np.floor(p[:, 0].min()))), max(cy0, int(np.floor(p[:, 1].min())))
                bx1, by1 = min(cx1, int(np.ceil(p[:, 0].max())) + 1), min(cy1, int(np.ceil(p[:, 1].max())) + 1)
                if bx0 >= bx1 or by0 >= by1:
                    continue
                ys, xs = np.mgrid[by0:by1, bx0:bx1]
                px, py = xs + 0.5, ys + 0.5
                (ax, ay), (bx, by), (qx, qy) = p
                den = (by - qy) * (ax - qx) + (qx - bx) * (ay - qy)
                if abs(den) < 1e-9:
                    continue
                l0 = ((by - qy) * (px - qx) + (qx - bx) * (py - qy)) / den
                l1 = ((qy - ay) * (px - qx) + (ax - qx) * (py - qy)) / den
                l2 = 1 - l0 - l1
                inside = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
                if not inside.any():
                    continue
                uv = tri['uv'].astype(np.float64)
                u = l0 * uv[0, 0] + l1 * uv[1, 0] + l2 * uv[2, 0]
                v = l0 * uv[0, 1] + l1 * uv[1, 1] + l2 * uv[2, 1]
                cols = np.stack([(tri['col'] >> s) & 0xFF for s in (0, 8, 16, 24)], axis=1).astype(np.float64) / 255
                col = l0[..., None] * cols[0] + l1[..., None] * cols[1] + l2[..., None] * cols[2]
                sx = np.clip((u * tw).astype(np.int64), 0, tw - 1)
                sy = np.clip((v * th).astype(np.int64), 0, th - 1)
                texel = texture[sy, sx]
                rgba = texel * col
                alpha = (rgba[..., 3] * inside)[..., None]
                region = canvas[by0:by1, bx0:bx1]
                region[:] = region * (1 - alpha) + rgba[..., :3] * alpha
    return Image.fromarray((np.clip(canvas, 0, 1) * 255).astype(np.uint8))


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    names, font, lists = read_preview(sys.argv[1])
    image = rasterize(names, font, lists, (1920, 1080), root, sys.argv[3] if len(sys.argv) > 3 else None)
    image.save(sys.argv[2])
    print('wrote', sys.argv[2])
