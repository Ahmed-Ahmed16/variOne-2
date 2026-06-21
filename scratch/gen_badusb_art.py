#!/usr/bin/env python3
"""Generate VariOne badUSB art payloads (no figlet/jp2a on host -> render with PIL).

Outputs two Ducky-script payloads mirroring sd_files/.../Bruce_t_Best.txt:
  - VariOne_art.txt : block-letter word-art of "VariOne"
  - Vemo_art.txt    : ASCII-picture of the mascot (Canva/mascot/vemo_bgless.png)

Each art line is emitted as `STRINGLN <line>` between a notepad header and a
VariOne tagline footer. Run on host only:  python3 scratch/gen_badusb_art.py
"""
import pyfiglet
from PIL import Image, ImageOps

# Charset proven typeable by the device HID (the chars Bruce_t_Best.txt uses and
# which render on hardware). The earlier '#'/'@'/'*'/'+'/':' art typed nothing
# because those glyphs are skipped by this layout -> we restrict to this set.
PROVEN = set(" '(),-./>\\_|`") | set(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
)

HEADER = [
    "REM VariOne BadUSB Art Showcase (fast demo reveal)",
    "DEFAULTSTRINGDELAY 0",  # 0ms between chars -> near-instant "shock" reveal
    "GUI r",
    "DELAY 400",
    "STRINGLN notepad.exe",
    "DELAY 600",
]
FOOTER = [
    "ENTER",
    "STRINGLN -- VariOne | CIC New Cairo",
]


def wrap(art_lines):
    out = list(HEADER)
    for ln in art_lines:
        out.append("STRINGLN " + ln)
    out.extend(FOOTER)
    return "\n".join(out) + "\n"


def word_art(text, font="standard"):
    """figlet word-art (line-art glyphs use only proven-typeable chars)."""
    art = pyfiglet.figlet_format(text, font=font)
    lines = [ln.rstrip() for ln in art.splitlines()]
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
    return lines


def picture(png_path, width=50):
    ramp = "Mo. "  # 3-level (dark/mid/light) proven chars -> cleaner, less noisy
    img = Image.open(png_path).convert("RGBA")
    box = img.split()[-1].getbbox()  # crop to the mascot, drop empty margins
    if box:
        img = img.crop(box)
    bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
    img = Image.alpha_composite(bg, img).convert("L")
    img = ImageOps.autocontrast(img, cutoff=2)  # push outlines dark, fills light
    aspect = img.height / img.width
    height = max(1, int(width * aspect * 0.5))  # 0.5 = char aspect correction
    img = img.resize((width, height))
    px = img.load()
    n = len(ramp)
    lines = []
    for y in range(height):
        row = "".join(ramp[min(n - 1, px[x, y] * n // 256)] for x in range(width))
        lines.append(row.rstrip() or " ")
    return lines


def main():
    vari = word_art("VariOne", font="epic")
    vemo = picture("Canva/mascot/happy.png", width=50)  # mascot picture, proven chars
    targets = {
        "VariOne_art.txt": vari,
        "Vemo_art.txt": vemo,
        # Merged demo: VariOne banner on top, Vemo mascot picture below.
        "VariOne_Vemo_art.txt": vari + ["", ""] + vemo,
    }
    # Audit: flag any glyph the device HID may skip.
    for name, art in targets.items():
        bad = sorted({c for ln in art for c in ln if c not in PROVEN})
        if bad:
            print(f"WARNING {name}: non-proven chars -> {bad!r}")
        else:
            print(f"OK {name}: all chars proven-typeable")

    import os
    for d in ("sd_files/BadUSB and BlueDucky", "badusb_payloads"):
        os.makedirs(d, exist_ok=True)
        for name, art in targets.items():
            path = os.path.join(d, name)
            with open(path, "w") as f:
                f.write(wrap(art))
            print(f"wrote {path} ({len(art)} art lines)")


if __name__ == "__main__":
    main()
