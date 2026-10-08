#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 czl_installer 的应用图标 app.ico（多尺寸，含 256x256）。

图形语义：深蓝色圆角方块 + 向下的安装箭头 + 底部托盘线。
在 16/20/24 等小尺寸下，箭头与托盘仍能清晰辨认。

用法：
    python tools/make_icon.py
输出：
    czl_installer/app.ico
"""

from __future__ import annotations

import os
import sys

from PIL import Image, ImageDraw

# 需要嵌入 ICO 的尺寸。Windows 会按显示场景自动挑选最合适的一档。
ICON_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]

# 用于多尺寸渲染的超采样尺寸（先大后缩，边缘更平滑）
SUPERSAMPLE = 1024

# 配色：深蓝渐变背景 + 白色箭头（与界面主色 #2b6cb0 呼应）
COLOR_TOP = (58, 130, 214)
COLOR_BOTTOM = (26, 74, 140)
COLOR_ACCENT = (255, 255, 255)
COLOR_TRAY = (168, 205, 240)


def lerp(a: int, b: int, t: float) -> int:
    return int(round(a + (b - a) * t))


def rounded_gradient_tile(size: int, radius_ratio: float = 0.22) -> Image.Image:
    """画一个带垂直渐变的圆角方块。"""
    tile = Image.new("RGBA", (size, size), (0, 0, 0, 0))

    # 渐变层
    gradient = Image.new("RGBA", (size, size))
    pixels = gradient.load()
    for y in range(size):
        t = y / max(1, size - 1)
        color = (
            lerp(COLOR_TOP[0], COLOR_BOTTOM[0], t),
            lerp(COLOR_TOP[1], COLOR_BOTTOM[1], t),
            lerp(COLOR_TOP[2], COLOR_BOTTOM[2], t),
            255,
        )
        for x in range(size):
            pixels[x, y] = color

    # 圆角遮罩
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, size - 1, size - 1), radius=int(size * radius_ratio), fill=255
    )

    tile.paste(gradient, (0, 0), mask)
    return tile


def draw_arrow(img: Image.Image) -> None:
    """在方块中央画一个向下的安装箭头。"""
    size = img.size[0]
    draw = ImageDraw.Draw(img)

    cx = size / 2.0
    shaft_w = size * 0.15          # 箭杆宽度
    shaft_top = size * 0.20        # 箭杆顶端
    head_top = size * 0.46         # 箭头三角的起点
    tip_y = size * 0.74            # 箭头尖端
    head_w = size * 0.42           # 三角底边宽度

    # 箭杆
    draw.rectangle(
        (cx - shaft_w / 2, shaft_top, cx + shaft_w / 2, head_top + size * 0.02),
        fill=COLOR_ACCENT,
    )

    # 箭头三角
    draw.polygon(
        [
            (cx - head_w / 2, head_top),
            (cx + head_w / 2, head_top),
            (cx, tip_y),
        ],
        fill=COLOR_ACCENT,
    )

    # 底部“安装托盘”横线
    tray_y = size * 0.83
    tray_h = max(2.0, size * 0.045)
    tray_w = size * 0.52
    draw.rounded_rectangle(
        (cx - tray_w / 2, tray_y, cx + tray_w / 2, tray_y + tray_h),
        radius=tray_h / 2,
        fill=COLOR_TRAY,
    )


def build_master() -> Image.Image:
    master = rounded_gradient_tile(SUPERSAMPLE)
    draw_arrow(master)
    return master


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    out_dir = os.path.normpath(os.path.join(here, ".."))
    out_path = os.path.join(out_dir, "app.ico")

    master = build_master()

    # Pillow 原生按 sizes 生成多分辨率 ICO（内部分别重采样，效果较好）
    master.save(out_path, format="ICO", sizes=[(s, s) for s in ICON_SIZES])

    # 同时导出一张 PNG，便于在 README / 界面中复用
    png_path = os.path.join(out_dir, "app.png")
    master.resize((256, 256), Image.LANCZOS).save(png_path)

    print(f"written: {out_path}")
    print(f"written: {png_path}")
    print(f"sizes  : {ICON_SIZES}")

    with open(out_path, "rb") as fp:
        head = fp.read(6)
        count = int.from_bytes(head[4:6], "little")
    print(f"ico entries: {count}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
