#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ScienceAndTheology pixel repaint pipeline.

用途：
    把 AI 生成的大尺寸物品图标自动处理成更适合 Godot 背包/UI 使用的
    64×64 / 32×32 像素风 PNG。

它不是完整替代 Aseprite 手工修图，而是用于：
    AI 生成图 -> 自动裁切/去棋盘背景/统一缩放/限色/加描边 -> 可进项目的初版图标

依赖：
    pip install pillow

示例：
    python tools/pixel_repaint.py \
        --input raw_assets/item_icons \
        --output resource/items/_processed \
        --size 64 \
        --also-32 \
        --remove-bg auto \
        --palette adaptive \
        --colors 24 \
        --outline

    python tools/pixel_repaint.py \
        --input raw_assets/item_icons/copper_pickaxe.png \
        --output resource/items/tools \
        --name copper_pickaxe_icon \
        --palette snt \
        --outline
"""

from __future__ import annotations

import argparse
import math
import os
import re
from collections import Counter, deque
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional, Sequence, Tuple

from PIL import Image, ImageChops, ImageEnhance, ImageFilter, ImageOps

RGBA = Tuple[int, int, int, int]
RGB = Tuple[int, int, int]

SUPPORTED_EXTS = {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tga"}


# ScienceAndTheology 64×64 图标规范中的基础调色板。
# 不是要求所有图标完全只用这些颜色，而是提供一个“项目统一感”的可选映射。
SNT_PALETTE_HEX = [
    # global neutral / outline
    "#1A1410", "#2A211C", "#4A433E", "#6B625B", "#CFC6BA", "#F1EBDD",

    # wood
    "#B4854E", "#8F643A", "#6F4B2B", "#4E321D", "#2D1C11",

    # copper
    "#FFC28A", "#D88954", "#B86439", "#8A4327", "#512116", "#5AA58C",

    # bronze
    "#F0C97D", "#C99A56", "#A3783F", "#6F4E27", "#3E2916",

    # steel / iron
    "#EDF2F4", "#B7C0C7", "#86929C", "#57616C", "#2F3842", "#9A5A34", "#6E3E28",

    # stone
    "#C1BAAD", "#999182", "#756D60", "#575147", "#302C27",

    # raw clay
    "#C79A85", "#B1785E", "#945A45", "#6D3D31", "#3F211C",

    # fired ceramic
    "#D07A54", "#B65D3D", "#8F432F", "#642C23", "#361612", "#2B2220", "#47403B",

    # blueprint
    "#7CC0FF", "#3B88E3", "#1E5FB8", "#163E7A", "#0C2148", "#F4F8FF", "#CBE3FF",

    # seeds / crops
    "#F8E39A", "#E0B754", "#B9852E", "#7D561E", "#432D11",
    "#9FD97A", "#67B256", "#47863D", "#2F5A2A", "#1A3118",

    # source law cyan glow
    "#E7FFFF", "#9CF7FF", "#4ED8E6", "#1E94B2", "#0C4250",
]


def hex_to_rgb(value: str) -> RGB:
    value = value.strip().lstrip("#")
    return int(value[0:2], 16), int(value[2:4], 16), int(value[4:6], 16)


SNT_PALETTE = [hex_to_rgb(c) for c in SNT_PALETTE_HEX]


@dataclass(frozen=True)
class ProcessOptions:
    size: int
    also_32: bool
    crop_padding: int
    colors: int
    palette: str
    remove_bg: str
    bg_tolerance: int
    alpha_threshold: int
    outline: bool
    outline_color: RGB
    outline_width: int
    contrast: float
    saturation: float
    sharpen: bool
    matte: RGB
    suffix: str
    preserve_names: bool
    name: Optional[str]


def sanitize_stem(stem: str) -> str:
    stem = stem.strip().lower()
    stem = re.sub(r"[^a-z0-9_\-]+", "_", stem)
    stem = re.sub(r"_+", "_", stem).strip("_")
    return stem or "icon"


def iter_input_files(input_path: Path, recursive: bool) -> Iterable[Path]:
    if input_path.is_file():
        if input_path.suffix.lower() in SUPPORTED_EXTS:
            yield input_path
        return

    if recursive:
        iterator = input_path.rglob("*")
    else:
        iterator = input_path.glob("*")

    for path in iterator:
        if path.is_file() and path.suffix.lower() in SUPPORTED_EXTS:
            yield path


def color_distance_sq(a: RGB, b: RGB) -> int:
    dr = a[0] - b[0]
    dg = a[1] - b[1]
    db = a[2] - b[2]
    return dr * dr + dg * dg + db * db


def is_low_saturation_bright(rgb: RGB) -> bool:
    r, g, b = rgb
    hi = max(rgb)
    lo = min(rgb)
    return hi >= 150 and (hi - lo) <= 35


def sample_border_bg_colors(img: Image.Image, max_colors: int = 4) -> List[RGB]:
    """从边缘采样最可能的透明棋盘/白底背景色。"""
    rgba = img.convert("RGBA")
    w, h = rgba.size
    pixels = rgba.load()
    samples: List[RGB] = []

    for x in range(w):
        samples.append(pixels[x, 0][:3])
        samples.append(pixels[x, h - 1][:3])
    for y in range(h):
        samples.append(pixels[0, y][:3])
        samples.append(pixels[w - 1, y][:3])

    # 量化到 8 色阶，避免 checkerboard 抗锯齿产生太多近似色。
    quantized = [tuple((c // 8) * 8 for c in rgb) for rgb in samples]
    counts = Counter(quantized)

    candidates: List[RGB] = []
    for rgb, _count in counts.most_common(16):
        if is_low_saturation_bright(rgb):
            candidates.append(rgb)
        if len(candidates) >= max_colors:
            break

    if not candidates:
        candidates = [counts.most_common(1)[0][0]]

    return candidates


def flood_remove_background(img: Image.Image, tolerance: int) -> Image.Image:
    """
    通过边缘 flood fill 去掉 AI 图常见的“烘焙棋盘透明背景”。

    只从画面边缘开始删除，避免把物体内部白色高光误删。
    对 image_gen 这种白灰棋盘背景比较有效。
    """
    rgba = img.convert("RGBA")
    w, h = rgba.size
    pix = rgba.load()
    bg_colors = sample_border_bg_colors(rgba)
    tolerance_sq = tolerance * tolerance

    def is_bg(x: int, y: int) -> bool:
        r, g, b, a = pix[x, y]
        if a == 0:
            return True
        rgb = (r, g, b)
        return any(color_distance_sq(rgb, bg) <= tolerance_sq for bg in bg_colors)

    q: deque[Tuple[int, int]] = deque()
    seen = set()

    for x in range(w):
        q.append((x, 0))
        q.append((x, h - 1))
    for y in range(h):
        q.append((0, y))
        q.append((w - 1, y))

    while q:
        x, y = q.popleft()
        if (x, y) in seen:
            continue
        seen.add((x, y))
        if not (0 <= x < w and 0 <= y < h):
            continue
        if not is_bg(x, y):
            continue

        r, g, b, _a = pix[x, y]
        pix[x, y] = (r, g, b, 0)

        if x > 0:
            q.append((x - 1, y))
        if x + 1 < w:
            q.append((x + 1, y))
        if y > 0:
            q.append((x, y - 1))
        if y + 1 < h:
            q.append((x, y + 1))

    return rgba


def hard_alpha(img: Image.Image, threshold: int) -> Image.Image:
    rgba = img.convert("RGBA")
    r, g, b, a = rgba.split()
    a = a.point(lambda v: 255 if v >= threshold else 0)
    return Image.merge("RGBA", (r, g, b, a))


def crop_to_alpha(img: Image.Image, padding: int) -> Image.Image:
    rgba = img.convert("RGBA")
    alpha = rgba.getchannel("A")
    bbox = alpha.getbbox()
    if bbox is None:
        return rgba

    x0, y0, x1, y1 = bbox
    w, h = rgba.size
    x0 = max(0, x0 - padding)
    y0 = max(0, y0 - padding)
    x1 = min(w, x1 + padding)
    y1 = min(h, y1 + padding)
    return rgba.crop((x0, y0, x1, y1))


def pad_to_square(img: Image.Image, matte: RGB = (0, 0, 0)) -> Image.Image:
    rgba = img.convert("RGBA")
    w, h = rgba.size
    side = max(w, h)
    out = Image.new("RGBA", (side, side), (matte[0], matte[1], matte[2], 0))
    out.alpha_composite(rgba, ((side - w) // 2, (side - h) // 2))
    return out


def enhance_image(img: Image.Image, contrast: float, saturation: float) -> Image.Image:
    rgba = img.convert("RGBA")
    if abs(contrast - 1.0) > 0.001:
        alpha = rgba.getchannel("A")
        rgb = rgba.convert("RGB")
        rgb = ImageEnhance.Contrast(rgb).enhance(contrast)
        rgba = Image.merge("RGBA", (*rgb.split(), alpha))
    if abs(saturation - 1.0) > 0.001:
        alpha = rgba.getchannel("A")
        rgb = rgba.convert("RGB")
        rgb = ImageEnhance.Color(rgb).enhance(saturation)
        rgba = Image.merge("RGBA", (*rgb.split(), alpha))
    return rgba


def quantize_adaptive(img: Image.Image, colors: int, matte: RGB) -> Image.Image:
    rgba = img.convert("RGBA")
    alpha = rgba.getchannel("A")

    matte_img = Image.new("RGBA", rgba.size, (matte[0], matte[1], matte[2], 255))
    matte_img.alpha_composite(rgba)
    rgb = matte_img.convert("RGB")

    # ADAPTIVE palette 适合每张图保留自身材质层次。
    q = rgb.quantize(colors=max(2, colors), method=Image.Quantize.MEDIANCUT)
    rgb_q = q.convert("RGB")
    return Image.merge("RGBA", (*rgb_q.split(), alpha))


def nearest_palette_color(rgb: RGB, palette: Sequence[RGB]) -> RGB:
    best = palette[0]
    best_d = 1 << 62
    for p in palette:
        # 绿色对视觉亮度影响大一点，给一点权重。
        dr = rgb[0] - p[0]
        dg = rgb[1] - p[1]
        db = rgb[2] - p[2]
        d = dr * dr * 3 + dg * dg * 4 + db * db * 2
        if d < best_d:
            best = p
            best_d = d
    return best


def quantize_snt_palette(img: Image.Image, alpha_threshold: int) -> Image.Image:
    rgba = img.convert("RGBA")
    pix = rgba.load()
    w, h = rgba.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = pix[x, y]
            if a < alpha_threshold:
                pix[x, y] = (r, g, b, 0)
            else:
                nr, ng, nb = nearest_palette_color((r, g, b), SNT_PALETTE)
                pix[x, y] = (nr, ng, nb, 255)
    return rgba


def add_pixel_outline(img: Image.Image, color: RGB, width: int) -> Image.Image:
    rgba = img.convert("RGBA")
    alpha = rgba.getchannel("A")

    # 膨胀 alpha，再减去原 alpha，得到外轮廓 mask。
    grown = alpha
    for _ in range(max(1, width)):
        grown = grown.filter(ImageFilter.MaxFilter(3))
    outline_mask = ImageChops.subtract(grown, alpha)

    outline = Image.new("RGBA", rgba.size, (color[0], color[1], color[2], 255))
    out = Image.new("RGBA", rgba.size, (0, 0, 0, 0))
    out.alpha_composite(Image.composite(outline, out, outline_mask))
    out.alpha_composite(rgba)
    return out


def process_one(src: Path, dst_stem: str, output_dir: Path, opt: ProcessOptions) -> Tuple[Path, Optional[Path]]:
    original = Image.open(src).convert("RGBA")

    if opt.remove_bg in {"auto", "checker"}:
        work = flood_remove_background(original, tolerance=opt.bg_tolerance)
    elif opt.remove_bg == "none":
        work = original
    else:
        raise ValueError(f"Unknown remove-bg mode: {opt.remove_bg}")

    work = hard_alpha(work, opt.alpha_threshold)
    work = crop_to_alpha(work, opt.crop_padding)
    work = pad_to_square(work, opt.matte)

    # 先用 LANCZOS 把 AI 大图压到 64，保留主体形体；后面再限色和硬化边缘。
    work = work.resize((opt.size, opt.size), Image.Resampling.LANCZOS)
    work = enhance_image(work, opt.contrast, opt.saturation)

    if opt.sharpen:
        work = work.filter(ImageFilter.SHARPEN)

    if opt.palette == "adaptive":
        work = quantize_adaptive(work, opt.colors, opt.matte)
    elif opt.palette == "snt":
        work = quantize_snt_palette(work, opt.alpha_threshold)
    elif opt.palette == "none":
        pass
    else:
        raise ValueError(f"Unknown palette mode: {opt.palette}")

    work = hard_alpha(work, opt.alpha_threshold)

    if opt.outline:
        work = add_pixel_outline(work, opt.outline_color, opt.outline_width)

    output_dir.mkdir(parents=True, exist_ok=True)
    out64 = output_dir / f"{dst_stem}{opt.suffix}_{opt.size}.png"
    work.save(out64, "PNG", optimize=True)

    out32: Optional[Path] = None
    if opt.also_32:
        out32 = output_dir / f"{dst_stem}{opt.suffix}_32.png"
        work.resize((32, 32), Image.Resampling.NEAREST).save(out32, "PNG", optimize=True)

    return out64, out32


def parse_rgb(value: str) -> RGB:
    value = value.strip()
    if value.startswith("#"):
        return hex_to_rgb(value)
    parts = [int(p.strip()) for p in value.split(",")]
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("RGB must be '#RRGGBB' or 'r,g,b'")
    return parts[0], parts[1], parts[2]


def make_output_stem(path: Path, opt: ProcessOptions, index: int, multiple: bool) -> str:
    if opt.name:
        if multiple:
            return f"{sanitize_stem(opt.name)}_{index:03d}"
        return sanitize_stem(opt.name)

    stem = sanitize_stem(path.stem)
    # 常见 AI 导出名太长，保留用户改过的短文件名更好。
    if not opt.preserve_names:
        stem = re.sub(r"^(a_|an_|the_)", "", stem)
        stem = stem[:80].strip("_") or "icon"
    return stem


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert AI-generated item art into unified 64x64 pixel-art PNG icons for ScienceAndTheology."
    )
    parser.add_argument("--input", "-i", required=True, type=Path, help="输入图片或图片目录")
    parser.add_argument("--output", "-o", required=True, type=Path, help="输出目录")
    parser.add_argument("--recursive", action="store_true", help="递归处理输入目录")

    parser.add_argument("--size", type=int, default=64, help="目标尺寸，默认 64")
    parser.add_argument("--also-32", action="store_true", help="额外生成 32×32 版本")
    parser.add_argument("--crop-padding", type=int, default=12, help="裁切透明边缘前保留的源图 padding")

    parser.add_argument("--remove-bg", choices=["auto", "checker", "none"], default="auto", help="是否自动去掉 AI 棋盘/浅色背景")
    parser.add_argument("--bg-tolerance", type=int, default=28, help="背景颜色容差，棋盘背景可尝试 20~45")
    parser.add_argument("--alpha-threshold", type=int, default=32, help="alpha 小于该值视为透明")

    parser.add_argument("--palette", choices=["adaptive", "snt", "none"], default="adaptive", help="限色方式")
    parser.add_argument("--colors", type=int, default=24, help="adaptive 模式颜色数，建议 16~32")
    parser.add_argument("--contrast", type=float, default=1.08, help="对比度增强，默认 1.08")
    parser.add_argument("--saturation", type=float, default=1.03, help="饱和度增强，默认 1.03")
    parser.add_argument("--no-sharpen", action="store_true", help="禁用轻微锐化")

    parser.add_argument("--outline", action="store_true", help="自动添加像素外描边")
    parser.add_argument("--outline-color", type=parse_rgb, default=parse_rgb("#1A1410"), help="描边颜色，默认 #1A1410")
    parser.add_argument("--outline-width", type=int, default=1, help="描边宽度，默认 1px")

    parser.add_argument("--matte", type=parse_rgb, default=parse_rgb("#1A1410"), help="透明像素量化时的底色，默认深描边色")
    parser.add_argument("--suffix", default="_icon", help="输出文件名后缀，默认 _icon；最终为 name_icon_64.png")
    parser.add_argument("--name", default=None, help="单文件输入时可指定输出基础名，例如 copper_pickaxe")
    parser.add_argument("--preserve-names", action="store_true", help="尽量保留原始文件名")

    args = parser.parse_args()

    files = list(iter_input_files(args.input, args.recursive))
    if not files:
        print(f"[WARN] no supported images found: {args.input}")
        return 1

    opt = ProcessOptions(
        size=args.size,
        also_32=args.also_32,
        crop_padding=args.crop_padding,
        colors=args.colors,
        palette=args.palette,
        remove_bg=args.remove_bg,
        bg_tolerance=args.bg_tolerance,
        alpha_threshold=args.alpha_threshold,
        outline=args.outline,
        outline_color=args.outline_color,
        outline_width=args.outline_width,
        contrast=args.contrast,
        saturation=args.saturation,
        sharpen=not args.no_sharpen,
        matte=args.matte,
        suffix=args.suffix,
        preserve_names=args.preserve_names,
        name=args.name,
    )

    multiple = len(files) > 1
    print(f"[INFO] processing {len(files)} image(s)")
    print(f"[INFO] output: {args.output}")

    for index, src in enumerate(files, start=1):
        try:
            stem = make_output_stem(src, opt, index, multiple)
            out64, out32 = process_one(src, stem, args.output, opt)
            if out32:
                print(f"[OK] {src} -> {out64}, {out32}")
            else:
                print(f"[OK] {src} -> {out64}")
        except Exception as exc:  # noqa: BLE001 - CLI tool should continue batch processing
            print(f"[ERROR] failed: {src}: {exc}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
