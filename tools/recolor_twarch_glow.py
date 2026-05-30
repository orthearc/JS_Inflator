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

# Title region (1300-space): painted flat navy, then logo+wordmark drawn on top.
TITLE_BOX = (18, 10, 410, 104)          # x0,y0,x1,y1
LOGO_XY, LOGO_H = (34, 18), 80          # logo top-left + height px
WORD, WORD_XY, WORD_SZ = "Inflator", (140, 14), 50
WORD_COLOR = (220, 232, 255, 255)       # uniform bright lavender-white
LOGO_COLOR = (92, 156, 236)             # accent blue
FONT = "C:/Windows/Fonts/segoeuib.ttf"

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
# flat navy title band
ImageDraw.Draw(img).rectangle(TITLE_BOX, fill=(int(NAVY[0]), int(NAVY[1]), int(NAVY[2]), 255))

# Arc ring logo: render SVG, crop content, recolor, paste
png = cairosvg.svg2png(url=LOGO_SVG, output_width=1024, output_height=1024)
logo = Image.open(io.BytesIO(png)).convert('RGBA')
logo = logo.crop(logo.getbbox())
w = int(LOGO_H * logo.width / logo.height)
logo = logo.resize((w, LOGO_H), Image.LANCZOS)
la = np.asarray(logo).astype(np.float32)
col = np.zeros_like(la); col[..., 0], col[..., 1], col[..., 2] = LOGO_COLOR; col[..., 3] = la[..., 3]
logo = Image.fromarray(col.astype(np.uint8), 'RGBA')
img.alpha_composite(logo, LOGO_XY)

# uniform wordmark
d = ImageDraw.Draw(img)
d.text(WORD_XY, WORD, font=ImageFont.truetype(FONT, WORD_SZ), fill=WORD_COLOR)

# alpha: void keying (keep title band opaque)
chroma = arr.max(2) - arr.min(2)
void = (L < 52) & (chroma < 18) & (text < 0.05) & (glow < 0.05)
void[TITLE_BOX[1]:TITLE_BOX[3], TITLE_BOX[0]:TITLE_BOX[2]] = False
alpha = np.asarray(img)[..., 3].copy()
alpha[void] = 0
res = np.dstack([np.asarray(img)[..., :3], alpha])
Image.fromarray(res, 'RGBA').save(OUT)
print('wrote', OUT, '| void %:', round(100*void.mean(), 1))
