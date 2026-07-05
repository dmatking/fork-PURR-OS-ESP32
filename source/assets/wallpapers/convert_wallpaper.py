#!/usr/bin/env python3
"""
convert_wallpaper.py — Cupcake custom wallpaper converter

Resizes/crops any image to a target panel resolution and writes a raw
RGB565 pixel file. Copy the output onto the device's SD card under
/wallpapers/, then pick it in Settings -> Wallpaper.

This does NOT get compiled into firmware or go through convert_icons.py's
pipeline — Cupcake loads it at runtime straight off the SD card via
fopen/fread, so there's no C-array step and no rebuild needed to add or
change a wallpaper.

Usage:
  python3 convert_wallpaper.py photo.jpg beach.rgb565
  python3 convert_wallpaper.py photo.jpg beach.rgb565 --width 320 --height 240
  python3 convert_wallpaper.py photo.jpg beach.rgb565 --swap   # for boards
                                                                # with CONFIG_LV_COLOR_16_SWAP=y

Dependencies: Pillow
  pip3 install Pillow
"""

import argparse
import struct
import sys

from PIL import Image, ImageOps


def convert(in_path, out_path, width, height, swap):
    img = Image.open(in_path).convert("RGB")
    img = ImageOps.fit(img, (width, height), Image.LANCZOS)

    out = bytearray(width * height * 2)
    pixels = img.load()
    i = 0
    for y in range(height):
        for x in range(width):
            r, g, b = pixels[x, y]
            rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            if swap:
                out[i]     = (rgb565 >> 8) & 0xFF
                out[i + 1] = rgb565 & 0xFF
            else:
                out[i]     = rgb565 & 0xFF
                out[i + 1] = (rgb565 >> 8) & 0xFF
            i += 2

    with open(out_path, "wb") as f:
        f.write(out)

    print(f"wrote {out_path}  ({width}x{height}, {len(out)} bytes, "
          f"{'swapped' if swap else 'native'} byte order)")
    print(f"copy this file to the SD card's /wallpapers/ folder, then pick it "
          f"in Settings -> Wallpaper on the device.")


def main():
    parser = argparse.ArgumentParser(
        prog="convert_wallpaper",
        description="Convert an image to a raw RGB565 wallpaper file for Cupcake",
    )
    parser.add_argument("input", help="source image (jpg/png/etc)")
    parser.add_argument("output", help="output .rgb565 file")
    parser.add_argument("--width", type=int, default=320, help="panel width (default 320)")
    parser.add_argument("--height", type=int, default=240, help="panel height (default 240)")
    parser.add_argument("--swap", action="store_true",
                         help="byte-swap each pixel — needed on boards built with "
                              "CONFIG_LV_COLOR_16_SWAP=y (check the device's "
                              "sdkconfig_<device>.overrides)")
    args = parser.parse_args()

    try:
        convert(args.input, args.output, args.width, args.height, args.swap)
    except FileNotFoundError:
        print(f"error: could not open '{args.input}'", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
