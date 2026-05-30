"""Recolor twarch_background.png to the Arcorrect navy palette with NEON-WHITE GLOW
on the engraved text/lines. Source = original (git main) background; output = RGBA
with transparent voids (for the starfield) and glowing ink. Tunable params at top."""
import numpy as np
from PIL import Image, ImageFilter

SRC = 'tools/_orig_bg.png'                 # original upstream background
OUT = 'resource/twarch/twarch_background.png'
NAVY  = np.array([0x10, 0x10, 0x1C], np.float32)   # panel base
WHITE = np.array([255, 255, 255], np.float32)
HALO  = np.array([140, 195, 255], np.float32)      # cyan-blue glow halo
CORE  = np.array([235, 244, 255], np.float32)      # neon-white text core
LOCAL_R = 10        # local-mean radius (panel vs ink)
GLOW_R  = 5         # glow halo blur radius
GLOW_GAIN = 1.15    # halo brightness
EDGE    = 6         # ink/contrast threshold
NORM    = 38.0      # ink strength normalizer (smaller = stronger text)

arr = np.asarray(Image.open(SRC).convert('RGB')).astype(np.float32)
L = 0.299*arr[..., 0] + 0.587*arr[..., 1] + 0.114*arr[..., 2]
local = np.asarray(Image.fromarray(L.astype(np.uint8)).filter(
    ImageFilter.GaussianBlur(LOCAL_R))).astype(np.float32)

# dark ink on bright panel + light text on dark window
ink  = np.where(local > 90, np.clip(local - L - EDGE, 0, None), 0)
lite = np.where(local < 95, np.clip(L - local - EDGE, 0, None), 0)
text = np.clip(np.maximum(ink, lite) / NORM, 0, 1)            # 0..1 text strength

# navy luminance-preserving base for non-text panel
Ln = L[..., None]
base = np.where(Ln <= 128, NAVY*(Ln/128.0),
                NAVY + (WHITE-NAVY)*((Ln-128)/127.0))

# glow halo = blurred text
glow = np.asarray(Image.fromarray((text*255).astype(np.uint8)).filter(
    ImageFilter.GaussianBlur(GLOW_R))).astype(np.float32)/255.0

out = base + HALO[None, None, :]*glow[..., None]*GLOW_GAIN     # add halo (lighten)
out = out*(1-text[..., None]) + CORE[None, None, :]*text[..., None]  # crisp core
out = np.clip(out, 0, 255).astype(np.uint8)

# void keying for starfield (dark flat, low chroma) — but never key glowing text
chroma = arr.max(2) - arr.min(2)
void = (L < 52) & (chroma < 18) & (text < 0.05) & (glow < 0.05)
alpha = np.where(void, 0, 255).astype(np.uint8)
Image.fromarray(np.dstack([out, alpha]), 'RGBA').save(OUT)
print('wrote', OUT, '| text px %:', round(100*(text > 0.15).mean(), 2),
      '| void px %:', round(100*void.mean(), 2))
