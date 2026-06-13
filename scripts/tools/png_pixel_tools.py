from __future__ import annotations

import math
import struct
import zlib
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple


RGBA = Tuple[int, int, int, int]

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _chunk(kind: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(kind)
    crc = zlib.crc32(data, crc)
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", crc & 0xFFFFFFFF)


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def _unfilter_row(filter_type: int, row: bytearray, previous: bytes, bpp: int) -> bytearray:
    out = bytearray(row)
    for i, value in enumerate(row):
        left = out[i - bpp] if i >= bpp else 0
        up = previous[i] if previous else 0
        up_left = previous[i - bpp] if previous and i >= bpp else 0
        if filter_type == 0:
            out[i] = value
        elif filter_type == 1:
            out[i] = (value + left) & 0xFF
        elif filter_type == 2:
            out[i] = (value + up) & 0xFF
        elif filter_type == 3:
            out[i] = (value + ((left + up) // 2)) & 0xFF
        elif filter_type == 4:
            out[i] = (value + _paeth(left, up, up_left)) & 0xFF
        else:
            raise ValueError(f"Unsupported PNG filter type {filter_type}.")
    return out


def read_png(path: str | Path) -> Tuple[int, int, List[RGBA]]:
    data = Path(path).read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError(f"{path} is not a PNG file.")

    offset = len(PNG_SIGNATURE)
    width = height = bit_depth = color_type = None
    interlace = 0
    palette: List[Tuple[int, int, int]] = []
    transparency = b""
    idat = bytearray()

    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError(f"Truncated PNG chunk in {path}.")
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        chunk_data = data[offset + 8:offset + 8 + length]
        offset += 12 + length

        if kind == b"IHDR":
            width, height, bit_depth, color_type, _compression, _filter, interlace = struct.unpack(
                ">IIBBBBB", chunk_data
            )
        elif kind == b"PLTE":
            palette = [
                (chunk_data[i], chunk_data[i + 1], chunk_data[i + 2])
                for i in range(0, len(chunk_data), 3)
            ]
        elif kind == b"tRNS":
            transparency = chunk_data
        elif kind == b"IDAT":
            idat.extend(chunk_data)
        elif kind == b"IEND":
            break

    if width is None or height is None or bit_depth is None or color_type is None:
        raise ValueError(f"Missing IHDR in {path}.")
    if bit_depth != 8:
        raise ValueError(f"Only 8-bit PNG files are supported, got bit depth {bit_depth}.")
    if interlace:
        raise ValueError("Interlaced PNG files are not supported by this lightweight tool.")

    channels_by_type = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
    if color_type not in channels_by_type:
        raise ValueError(f"Unsupported PNG color type {color_type}.")

    channels = channels_by_type[color_type]
    row_len = width * channels
    raw = zlib.decompress(bytes(idat))
    expected = height * (row_len + 1)
    if len(raw) < expected:
        raise ValueError(f"PNG pixel data is shorter than expected in {path}.")

    rows: List[bytes] = []
    previous = bytes(row_len)
    pos = 0
    for _y in range(height):
        filter_type = raw[pos]
        pos += 1
        encoded = bytearray(raw[pos:pos + row_len])
        pos += row_len
        row = _unfilter_row(filter_type, encoded, previous, channels)
        rows.append(bytes(row))
        previous = bytes(row)

    pixels: List[RGBA] = []
    transparent_gray = None
    transparent_rgb = None
    if color_type == 0 and len(transparency) >= 2:
        transparent_gray = struct.unpack(">H", transparency[:2])[0] & 0xFF
    elif color_type == 2 and len(transparency) >= 6:
        transparent_rgb = tuple(v & 0xFF for v in struct.unpack(">HHH", transparency[:6]))

    for row in rows:
        if color_type == 6:
            for i in range(0, len(row), 4):
                pixels.append((row[i], row[i + 1], row[i + 2], row[i + 3]))
        elif color_type == 2:
            for i in range(0, len(row), 3):
                rgb = (row[i], row[i + 1], row[i + 2])
                alpha = 0 if transparent_rgb == rgb else 255
                pixels.append((rgb[0], rgb[1], rgb[2], alpha))
        elif color_type == 4:
            for i in range(0, len(row), 2):
                pixels.append((row[i], row[i], row[i], row[i + 1]))
        elif color_type == 0:
            for value in row:
                alpha = 0 if transparent_gray == value else 255
                pixels.append((value, value, value, alpha))
        elif color_type == 3:
            for index in row:
                if index >= len(palette):
                    raise ValueError(f"Palette index {index} is out of range in {path}.")
                r, g, b = palette[index]
                alpha = transparency[index] if index < len(transparency) else 255
                pixels.append((r, g, b, alpha))

    return width, height, pixels


def write_png(path: str | Path, width: int, height: int, pixels: Sequence[RGBA]) -> None:
    if len(pixels) != width * height:
        raise ValueError("Pixel count does not match image dimensions.")

    raw = bytearray()
    for y in range(height):
        raw.append(0)
        row_start = y * width
        for r, g, b, a in pixels[row_start:row_start + width]:
            raw.extend((r & 0xFF, g & 0xFF, b & 0xFF, a & 0xFF))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    payload = (
        PNG_SIGNATURE
        + _chunk(b"IHDR", ihdr)
        + _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + _chunk(b"IEND", b"")
    )
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


def new_image(width: int, height: int, color: RGBA = (0, 0, 0, 0)) -> List[RGBA]:
    return [color for _ in range(width * height)]


def crop(
    pixels: Sequence[RGBA],
    width: int,
    _height: int,
    x: int,
    y: int,
    crop_width: int,
    crop_height: int,
) -> List[RGBA]:
    out: List[RGBA] = []
    for row in range(crop_height):
        start = (y + row) * width + x
        out.extend(pixels[start:start + crop_width])
    return out


def paste(
    dest: List[RGBA],
    dest_width: int,
    dest_height: int,
    source: Sequence[RGBA],
    source_width: int,
    source_height: int,
    dest_x: int,
    dest_y: int,
) -> None:
    for y in range(source_height):
        target_y = dest_y + y
        if target_y < 0 or target_y >= dest_height:
            continue
        for x in range(source_width):
            target_x = dest_x + x
            if target_x < 0 or target_x >= dest_width:
                continue
            dest[target_y * dest_width + target_x] = source[y * source_width + x]


def resize_nearest(
    pixels: Sequence[RGBA],
    width: int,
    height: int,
    target_width: int,
    target_height: int,
) -> List[RGBA]:
    if target_width <= 0 or target_height <= 0:
        raise ValueError("Target dimensions must be positive.")

    out: List[RGBA] = []
    for y in range(target_height):
        src_y = min(height - 1, int(y * height / target_height))
        for x in range(target_width):
            src_x = min(width - 1, int(x * width / target_width))
            out.append(pixels[src_y * width + src_x])
    return out


def flip_horizontal(pixels: Sequence[RGBA], width: int, height: int) -> List[RGBA]:
    out: List[RGBA] = []
    for y in range(height):
        row = pixels[y * width:(y + 1) * width]
        out.extend(reversed(row))
    return out


def color_from_hex(hex_color: str) -> RGBA:
    clean = hex_color.strip().lstrip("#")
    if len(clean) != 6:
        raise ValueError(f"Expected RRGGBB hex color, got {hex_color!r}.")
    return (
        int(clean[0:2], 16),
        int(clean[2:4], 16),
        int(clean[4:6], 16),
        255,
    )


def mix_color(a: RGBA, b: RGBA, t: float) -> RGBA:
    clamped = max(0.0, min(1.0, t))
    return (
        round(a[0] + (b[0] - a[0]) * clamped),
        round(a[1] + (b[1] - a[1]) * clamped),
        round(a[2] + (b[2] - a[2]) * clamped),
        round(a[3] + (b[3] - a[3]) * clamped),
    )


def color_distance(a: RGBA, b: RGBA) -> float:
    return math.sqrt(
        (a[0] - b[0]) ** 2
        + (a[1] - b[1]) ** 2
        + (a[2] - b[2]) ** 2
    )


def bounding_box_non_key(
    pixels: Sequence[RGBA],
    width: int,
    height: int,
    key: RGBA = (255, 0, 255, 255),
    tolerance: int = 90,
) -> Tuple[int, int, int, int] | None:
    min_x = width
    min_y = height
    max_x = -1
    max_y = -1
    for y in range(height):
        for x in range(width):
            pixel = pixels[y * width + x]
            if pixel[3] > 0 and color_distance(pixel, key) > tolerance:
                min_x = min(min_x, x)
                min_y = min(min_y, y)
                max_x = max(max_x, x)
                max_y = max(max_y, y)
    if max_x < min_x or max_y < min_y:
        return None
    return min_x, min_y, max_x, max_y


def chroma_cutout(
    pixels: Sequence[RGBA],
    key: RGBA = (255, 0, 255, 255),
    tolerance: int = 90,
) -> List[RGBA]:
    out: List[RGBA] = []
    for pixel in pixels:
        if pixel[3] == 0 or color_distance(pixel, key) <= tolerance:
            out.append((0, 0, 0, 0))
        else:
            out.append(pixel)
    return out
