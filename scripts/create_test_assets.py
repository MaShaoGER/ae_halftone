#!/usr/bin/env python3
"""Create small, dependency-free PNG inputs for the AE demo."""
from pathlib import Path
import math
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "examples/assets"


def png(path, width, height, pixels):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    raw = b"".join(b"\x00" + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def inside(x, y, vertices):
    hit = False
    j = len(vertices) - 1
    for i, (xi, yi) in enumerate(vertices):
        xj, yj = vertices[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            hit = not hit
        j = i
    return hit


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    vertices = []
    for i in range(10):
        angle = i * math.pi / 5 - math.pi / 2
        radius = 44 if i % 2 == 0 else 19
        vertices.append((48 + math.cos(angle) * radius, 48 + math.sin(angle) * radius))
    pixels = bytearray()
    for y in range(96):
        for x in range(96):
            covered = sum(inside(x + (i + .5) / 4, y + (j + .5) / 4, vertices) for j in range(4) for i in range(4))
            pixels.extend((255, 193, 67, round(255 * covered / 16)))
    png(OUT / "star.png", 96, 96, pixels)
    pixels = bytearray()
    for _ in range(64):
        for x in range(256):
            pixels.extend((x, x, x, 255))
    png(OUT / "gradient-profile.png", 256, 64, pixels)
    print("Demo assets:", OUT)


if __name__ == "__main__":
    main()
