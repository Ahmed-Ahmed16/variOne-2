#!/usr/bin/env python3
"""
extract_head.py — crop the Vemo head out of a device screenshot and save it named
by reaction, alongside vemo_head.png in art/vemo/device/.

Device scan/status screens are: small Vemo head on the LEFT, status text on the RIGHT
(e.g. "Success!", "Thinking...", Zzz). This tool keeps only the head:
  1. looks at the left portion of the image,
  2. auto-crops to the bright (non-black) bounding box (the head),
  3. squares + pads it, resizes to a head-box size (default 80 px, must stay <=160 px wide
     for the device PNG decoder),
  4. saves as art/vemo/device/vemo_head_<reaction>.png.

Usage:
  python3 extract_head.py <reaction> <screenshot.png> [more_reaction other.png ...]

Examples:
  python3 extract_head.py sleeping zzz.png success ok.png thinking think.png

Reaction names should match the mood map in art/vemo/README.md, e.g.:
  idle, blink, working, scan, success, fail, sad, thinking, sleeping, waving.
"""
import sys
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.normpath(os.path.join(HERE, "..", "device"))
HEAD_PX = 80          # output size (square); keep <= 160
LEFT_FRACTION = 0.50  # head lives in the left half of the screenshot
DARK = 40             # pixel value <= DARK on all channels = background


def content_bbox(img, region):
    """Bounding box of non-dark pixels within `region` (l,t,r,b)."""
    l, t, r, b = region
    px = img.load()
    minx, miny, maxx, maxy = r, b, l, t
    found = False
    for y in range(t, b):
        for x in range(l, r):
            pr, pg, pb = px[x, y][:3]
            if pr > DARK or pg > DARK or pb > DARK:
                found = True
                minx, miny = min(minx, x), min(miny, y)
                maxx, maxy = max(maxx, x), max(maxy, y)
    if not found:
        return None
    return (minx, miny, maxx + 1, maxy + 1)


def extract(reaction, path):
    img = Image.open(path).convert("RGBA")
    W, H = img.size
    region = (0, 0, int(W * LEFT_FRACTION), H)
    bbox = content_bbox(img.convert("RGB"), region)
    if bbox is None:
        print(f"  ! no head content found in left half of {path}")
        return
    head = img.crop(bbox)
    # square it (pad transparent to center)
    side = max(head.size)
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.paste(head, ((side - head.width) // 2, (side - head.height) // 2))
    sq = sq.resize((HEAD_PX, HEAD_PX), Image.LANCZOS)
    out = os.path.join(OUT_DIR, f"vemo_head_{reaction}.png")
    sq.save(out)
    print(f"  -> {out}  ({bbox[2]-bbox[0]}x{bbox[3]-bbox[1]} -> {HEAD_PX}x{HEAD_PX})")


def main(argv):
    if len(argv) < 3 or len(argv) % 2 == 0:
        print(__doc__)
        return 1
    os.makedirs(OUT_DIR, exist_ok=True)
    pairs = list(zip(argv[1::2], argv[2::2]))
    for reaction, path in pairs:
        if not os.path.isfile(path):
            print(f"  ! missing file: {path}")
            continue
        print(f"[{reaction}] {path}")
        extract(reaction, path)
    print("Done. Review the crops; tweak HEAD_PX / LEFT_FRACTION / DARK if the box is off.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
