#!/usr/bin/env python3
"""Generate crisp anti-aliased TypeCount mark PNG and multi-size Windows ICO."""
import math
import os
import struct
import zlib


def write_png(path, width, height, rgba_bytes):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw.extend(rgba_bytes[y * stride : (y + 1) * stride])

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    compressed = zlib.compress(bytes(raw), 9)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", compressed))
        f.write(chunk(b"IEND", b""))


def encode_png_bytes(width, height, rgba_bytes):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw.extend(rgba_bytes[y * stride : (y + 1) * stride])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")


def sd_rounded_box(px, py, cx, cy, hw, hh, r):
    qx = abs(px - cx) - (hw - r)
    qy = abs(py - cy) - (hh - r)
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r


def sd_segment(px, py, ax, ay, bx, by):
    pax, pay = px - ax, py - ay
    bax, bay = bx - ax, by - ay
    denom = bax * bax + bay * bay
    h = 0.0 if denom == 0 else max(0.0, min(1.0, (pax * bax + pay * bay) / denom))
    return math.hypot(pax - bax * h, pay - bay * h)


def blend(dst, src, alpha):
    if alpha <= 0.0:
        return dst
    if alpha >= 1.0:
        return src
    return (
        dst[0] * (1.0 - alpha) + src[0] * alpha,
        dst[1] * (1.0 - alpha) + src[1] * alpha,
        dst[2] * (1.0 - alpha) + src[2] * alpha,
        min(1.0, dst[3] + alpha * (1.0 - dst[3])),
    )


def lerp_color(c1, c2, t):
    t = max(0.0, min(1.0, t))
    return (
        c1[0] + (c2[0] - c1[0]) * t,
        c1[1] + (c2[1] - c1[1]) * t,
        c1[2] + (c2[2] - c1[2]) * t,
        1.0,
    )


def render_icon(size):
    out = bytearray(size * size * 4)
    aa = 1.15 / size * 128.0
    # Polyline points in 128x128 normalized coordinates
    pts = [
        (36.0, 55.0),
        (48.0, 55.0),
        (55.0, 38.0),
        (64.0, 68.0),
        (72.0, 48.0),
        (78.0, 55.0),
        (92.0, 55.0),
    ]
    c_navy = (21.0 / 255.0, 55.0 / 255.0, 104.0 / 255.0, 1.0)
    c_blue = (37.0 / 255.0, 99.0 / 255.0, 235.0 / 255.0, 1.0)
    c_cyan = (56.0 / 255.0, 189.0 / 255.0, 248.0 / 255.0, 1.0)
    c_sky = (14.0 / 255.0, 165.0 / 255.0, 233.0 / 255.0, 1.0)
    c_shadow = (15.0 / 255.0, 37.0 / 255.0, 71.0 / 255.0, 1.0)
    c_cap_top = (1.0, 1.0, 1.0, 1.0)
    c_cap_bot = (219.0 / 255.0, 234.0 / 255.0, 254.0 / 255.0, 1.0)

    def cov(d):
        return max(0.0, min(1.0, 0.5 - d / aa))

    for y in range(size):
        ny = (y + 0.5) / size * 128.0
        for x in range(size):
            nx = (x + 0.5) / size * 128.0
            bg_d = sd_rounded_box(nx, ny, 64.0, 64.0, 58.0, 58.0, 28.0)
            bg_a = cov(bg_d)
            if bg_a <= 0.0:
                continue
            grad_t = (nx + ny) / 256.0
            if grad_t < 0.55:
                bg_col = lerp_color(c_navy, c_blue, grad_t / 0.55)
            else:
                bg_col = lerp_color(c_blue, c_cyan, (grad_t - 0.55) / 0.45)

            col = (bg_col[0], bg_col[1], bg_col[2], bg_a)

            # Keycap base shadow
            sh_d = sd_rounded_box(nx, ny, 64.0, 61.0, 40.0, 37.0, 16.0)
            col = blend(col, c_shadow, cov(sh_d) * 0.35 * bg_a)

            # Keycap outer bevel
            cap_d = sd_rounded_box(nx, ny, 64.0, 56.0, 40.0, 36.0, 16.0)
            cap_t = (ny - 20.0) / 72.0
            cap_col = lerp_color(c_cap_top, c_cap_bot, cap_t)
            col = blend(col, cap_col, cov(cap_d) * bg_a)

            # Keycap inner face
            face_d = sd_rounded_box(nx, ny, 64.0, 53.0, 33.0, 27.0, 11.0)
            col = blend(col, (1.0, 1.0, 1.0, 1.0), cov(face_d) * bg_a)

            # Pulse line
            min_seg = 999.0
            for i in range(len(pts) - 1):
                ax, ay = pts[i]
                bx, by = pts[i + 1]
                min_seg = min(min_seg, sd_segment(nx, ny, ax, ay, bx, by))
            pulse_d = min_seg - 3.25
            col = blend(col, c_blue, cov(pulse_d) * bg_a)

            # Terminal pulse dot
            dot_d = math.hypot(nx - 94.0, ny - 55.0) - 4.2
            col = blend(col, c_sky, cov(dot_d) * bg_a)

            idx = (y * size + x) * 4
            out[idx] = int(round(max(0.0, min(1.0, col[0])) * 255))
            out[idx + 1] = int(round(max(0.0, min(1.0, col[1])) * 255))
            out[idx + 2] = int(round(max(0.0, min(1.0, col[2])) * 255))
            out[idx + 3] = int(round(max(0.0, min(1.0, col[3])) * 255))
    return bytes(out)


def write_ico(path, sizes=(16, 24, 32, 48, 64, 128, 256)):
    images = []
    for sz in sizes:
        rgba = render_icon(sz)
        png = encode_png_bytes(sz, sz, rgba)
        images.append((sz, png))

    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = bytearray()
    payload = bytearray()
    for sz, png in images:
        dim = 0 if sz >= 256 else sz
        entries.extend(struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(png), offset))
        payload.extend(png)
        offset += len(png)
    with open(path, "wb") as f:
        f.write(header + bytes(entries) + bytes(payload))


def main():
    os.makedirs("assets", exist_ok=True)
    mark = render_icon(128)
    write_png("assets/typecount-mark.png", 128, 128, mark)
    write_ico("assets/typecount.ico")
    print("Generated assets/typecount-mark.png and assets/typecount.ico")


if __name__ == "__main__":
    main()
