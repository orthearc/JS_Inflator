"""Recolor twarch_background.png to the Arcorrect navy palette.
- Engraved panel text/lines -> CLEAN bright (subtle glow, tunable).
- Top-left title region -> flat navy, then a uniform "Inflator" wordmark + the Arc ring logo.
- Dark voids -> transparent (for the starfield).
Source = original (git main) background. Tunable params at top."""
import numpy as np
from PIL import Image, ImageFilter, ImageDraw, ImageFont
import cairosvg, io

SRC = 'tools/_orig_bg.png'
OUT = 'resource/twarch/twarch_background.png'
LOGO_SVG = r'E:\The Arc\Arcwork\The Arc NEW LOGO DATLASH\THE ARC LOGO.svg'

NAVY  = np.array([0x10, 0x10, 0x1C], np.float32)
WHITE = np.array([255, 255, 255], np.float32)
HALO  = np.array([150, 200, 255], np.float32)      # subtle cyan-blue glow
CORE  = np.array([225, 236, 255], np.float32)       # clean bright text core
LOCAL_R   = 10
GLOW_R    = 2.0     # tight halo  (was 5)
GLOW_GAIN = 0.35    # low glow    (was 1.15)
EDGE  = 6
NORM  = 34.0

# Title region (1300-space): painted flat navy, then ORIGINAL 'Inflator' glyphs
# (recolored to one uniform color) + the Arc logo aligned to the title's vcenter.
TITLE_BOX  = (8, 10, 410, 100)          # area painted flat navy
GLYPH_BOX  = (55, 22, 300, 92)          # where the original 'Inflator' letters live
TITLE_VCENTER = 53                       # vertical center of the 'Inflator' text
WORD_COLOR = (220, 232, 255)            # uniform bright lavender-white
LOGO_X, LOGO_H = 12, 50                  # logo left-x + height; vcenter-aligned to title
LOGO_COLOR = (96, 160, 238)             # accent blue

arr = np.asarray(Image.open(SRC).convert('RGB')).astype(np.float32)
L = 0.299*arr[..., 0] + 0.587*arr[..., 1] + 0.114*arr[..., 2]
local = np.asarray(Image.fromarray(L.astype(np.uint8)).filter(
    ImageFilter.GaussianBlur(LOCAL_R))).astype(np.float32)

ink  = np.where(local > 90, np.clip(local - L - EDGE, 0, None), 0)
lite = np.where(local < 95, np.clip(L - local - EDGE, 0, None), 0)
text = np.clip(np.maximum(ink, lite) / NORM, 0, 1)
text[TITLE_BOX[1]:TITLE_BOX[3], TITLE_BOX[0]:TITLE_BOX[2]] = 0   # suppress glow in title box

Ln = L[..., None]
base = np.where(Ln <= 128, NAVY*(Ln/128.0), NAVY + (WHITE-NAVY)*((Ln-128)/127.0))
glow = np.asarray(Image.fromarray((text*255).astype(np.uint8)).filter(
    ImageFilter.GaussianBlur(GLOW_R))).astype(np.float32)/255.0

out = base + HALO[None, None, :]*glow[..., None]*GLOW_GAIN
out = out*(1-text[..., None]) + CORE[None, None, :]*text[..., None]
out = np.clip(out, 0, 255).astype(np.uint8)

img = Image.fromarray(out, 'RGB').convert('RGBA')
# flat navy over the whole title band (covers the messy embossed original)
ImageDraw.Draw(img).rectangle(TITLE_BOX, fill=(int(NAVY[0]), int(NAVY[1]), int(NAVY[2]), 255))

# ORIGINAL 'Inflator' letterforms -> uniform color (anti-aliased via luminance->alpha)
src = np.asarray(Image.open(SRC).convert('RGB').crop(GLYPH_BOX)).astype(np.float32)
Lg = 0.299*src[..., 0] + 0.587*src[..., 1] + 0.114*src[..., 2]
ga = np.clip((Lg - 65) / 95.0, 0, 1)                 # band(46)->0, light letters->1
glyph = np.zeros((ga.shape[0], ga.shape[1], 4), np.uint8)
glyph[..., 0], glyph[..., 1], glyph[..., 2] = WORD_COLOR
glyph[..., 3] = (ga*255).astype(np.uint8)
img.alpha_composite(Image.fromarray(glyph, 'RGBA'), (GLYPH_BOX[0], GLYPH_BOX[1]))

# Arc ring logo: render SVG, crop content, recolor, vcenter-align to the title
png = cairosvg.svg2png(url=LOGO_SVG, output_width=1024, output_height=1024)
logo = Image.open(io.BytesIO(png)).convert('RGBA')
logo = logo.crop(logo.getbbox())
lw = int(LOGO_H * logo.width / logo.height)
logo = logo.resize((lw, LOGO_H), Image.LANCZOS)
la = np.asarray(logo).astype(np.float32)
col = np.zeros_like(la); col[..., 0], col[..., 1], col[..., 2] = LOGO_COLOR; col[..., 3] = la[..., 3]
img.alpha_composite(Image.fromarray(col.astype(np.uint8), 'RGBA'),
                    (LOGO_X, TITLE_VCENTER - LOGO_H//2))

# alpha: void keying (keep title band opaque)
chroma = arr.max(2) - arr.min(2)
void = (L < 52) & (chroma < 18) & (text < 0.05) & (glow < 0.05)
void[TITLE_BOX[1]:TITLE_BOX[3], TITLE_BOX[0]:TITLE_BOX[2]] = False
alpha = np.asarray(img)[..., 3].copy()
alpha[void] = 0
res = np.dstack([np.asarray(img)[..., :3], alpha])
Image.fromarray(res, 'RGBA').save(OUT)
print('wrote', OUT, '| void %:', round(100*void.mean(), 1))
