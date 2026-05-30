#!/usr/bin/env python3
"""Punch transparent holes in the recolored Twarch background so the animated
StarfieldView (placed beneath it) shows through the dark "void" regions.

Phase 7 of the Arc Inflator skin. The recolored ``twarch_background.png`` is an
opaque RGB image whose empty panel areas are a near-black navy (around
``#06060A``). We convert it to RGBA and set alpha=0 ONLY for pixels that are
both very dark AND very low-chroma — i.e. the flat void. Every other pixel
(panel chrome, console, text, control housings, shading) stays fully opaque.

Key (CONSERVATIVE — better to under-key than to hole the panel graphics):

    luminance  = 0.299*R + 0.587*G + 0.114*B   < LUM_MAX (28)
    chroma     = max(R,G,B) - min(R,G,B)        < CHROMA_MAX (12)
        -> alpha = 0   (transparent void)
    otherwise -> alpha = 255 (opaque)

Measured on the recolored background, the void cluster is ``(6,6,10)`` /
``(5,5,9)`` (lum ~6, chroma 4) — comfortably inside the key — while the first
panel band is ``(35,35,46)`` (lum ~36) which is left opaque.

Dimensions are never changed. Saved in place as RGBA.

PIL (Pillow) required. Run with:  python tools/key_background.py
"""

import os
import sys

from PIL import Image

# --- conservative void key ------------------------------------------------
LUM_MAX = 28.0     # only pixels darker than this can be keyed
CHROMA_MAX = 12    # ...and only if near-greyscale (flat, not a colored detail)

BG_PATH = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "resource",
    "twarch",
    "twarch_background.png",
)


def key_background(path):
    img = Image.open(path)
    orig_size = img.size
    work = img.convert("RGBA")
    px = work.load()
    w, h = work.size

    keyed = 0
    total = w * h
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            lum = 0.299 * r + 0.587 * g + 0.114 * b
            chroma = max(r, g, b) - min(r, g, b)
            if lum < LUM_MAX and chroma < CHROMA_MAX:
                px[x, y] = (r, g, b, 0)   # transparent void
                keyed += 1
            # else: leave fully opaque (alpha already 255 after convert)

    assert work.size == orig_size, (
        f"size changed for {path}: {orig_size} -> {work.size}"
    )
    work.save(path)
    return total, keyed


def main():
    if not os.path.isfile(BG_PATH):
        print(f"ERROR: missing {BG_PATH}", file=sys.stderr)
        return 1
    total, keyed = key_background(BG_PATH)
    pct = 100.0 * keyed / total if total else 0.0
    print(
        f"twarch_background.png -> RGBA; keyed {keyed}/{total} pixels "
        f"({pct:.2f}%) transparent (lum<{LUM_MAX:g}, chroma<{CHROMA_MAX})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
