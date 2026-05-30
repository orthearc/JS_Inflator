#!/usr/bin/env python3
"""Re-tint the Twarch GUI bitmaps to the Arcorrect palette.

Phase 6 of the Arc Inflator skin. Each source bitmap is recolored IN PLACE
using a luminance-preserving hue shift that keeps the original 3D shading
(highlights / shadows) intact while mapping the grey/red/yellow Twarch hues
onto the Arcorrect blue accent (or a dark navy anchor for the background).

Method (per pixel):
  L = 0.299*R + 0.587*G + 0.114*B            # luminance, 0..255
  if L <= 128:  rgb = anchor * (L / 128)      # black -> anchor ramp
  else:         rgb = anchor + (white-anchor) * ((L-128)/127)  # anchor -> white
Alpha is left unchanged. RGB images (no alpha, e.g. the background) are
handled too: their channels are recolored, no alpha to preserve.

Dimensions are never resized: we load, transform pixels, and save with the
exact same size/mode. The script asserts size is unchanged before saving.

PIL (Pillow) required. Run with:  python tools/recolor_twarch.py
"""

import os
import sys

from PIL import Image

# --- Arcorrect palette anchors --------------------------------------------
ACCENT = (0x50, 0x90, 0xE0)   # medium blue   -> knobs / handle / buttons / meters
NAVY   = (0x10, 0x10, 0x1C)   # dark navy     -> background panel
WHITE  = (0xFF, 0xFF, 0xFF)   # highlight target

# Files re-tinted to the ACCENT blue.
ACCENT_FILES = [
    "twarch_knob_red_.png",
    "twarch_knob_yellow_.png",
    "twarch_handle.png",
    "twarch_btn_IN.png",
    "twarch_btn_SPLIT.png",
    "twarch_btn_CLIP.png",
    "twarch_meter_on.png",
    "twarch_meter_fx_on.png",
    "twarch_meter_off.png",
]

# Files re-tinted to the dark NAVY anchor (so the panel reads dark, not bright).
NAVY_FILES = [
    "twarch_background.png",
]

TWARCH_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "resource",
    "twarch",
)


def _build_lut(anchor):
    """Return a 256-entry list of (r, g, b) tuples mapping luminance -> RGB."""
    ar, ag, ab = anchor
    wr, wg, wb = WHITE
    lut = []
    for L in range(256):
        if L <= 128:
            t = L / 128.0
            rgb = (ar * t, ag * t, ab * t)
        else:
            t = (L - 128) / 127.0
            rgb = (
                ar + (wr - ar) * t,
                ag + (wg - ag) * t,
                ab + (wb - ab) * t,
            )
        lut.append(tuple(int(round(c)) for c in rgb))
    return lut


def recolor(path, anchor):
    img = Image.open(path)
    orig_size = img.size
    orig_mode = img.mode

    has_alpha = orig_mode in ("RGBA", "LA") or "transparency" in img.info
    work = img.convert("RGBA")
    px = work.load()
    w, h = work.size

    lut = _build_lut(anchor)

    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            # luminance 0..255 (rounded)
            L = int(round(0.299 * r + 0.587 * g + 0.114 * b))
            if L > 255:
                L = 255
            nr, ng, nb = lut[L]
            px[x, y] = (nr, ng, nb, a)

    # Restore original mode so we don't silently add/strip an alpha channel.
    if has_alpha:
        out = work
    else:
        out = work.convert("RGB")

    assert out.size == orig_size, (
        f"size changed for {path}: {orig_size} -> {out.size}"
    )

    out.save(path)
    return orig_size, orig_mode, out.mode


def main():
    if not os.path.isdir(TWARCH_DIR):
        print(f"ERROR: dir not found: {TWARCH_DIR}", file=sys.stderr)
        return 1

    jobs = [(f, ACCENT) for f in ACCENT_FILES] + [(f, NAVY) for f in NAVY_FILES]

    for fname, anchor in jobs:
        path = os.path.join(TWARCH_DIR, fname)
        if not os.path.isfile(path):
            print(f"ERROR: missing {path}", file=sys.stderr)
            return 1
        size, in_mode, out_mode = recolor(path, anchor)
        anchor_name = "NAVY" if anchor is NAVY else "ACCENT"
        print(
            f"{fname:28s} size={size[0]}x{size[1]} "
            f"mode={in_mode}->{out_mode} anchor={anchor_name}"
        )

    print("Done. All dimensions preserved (asserted per file).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
