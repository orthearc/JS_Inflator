"""Regenerate the Twarch VU-meter bitmaps as SEGMENTED ("brick") gradients,
reusing the ORIGINAL bitmap's segment alpha (the LED gaps), revealed bottom-up:
 - twarch_meter_on    : magenta(top/loud) -> purple -> cyan(bottom/quiet) bricks (IN/OUT)
 - twarch_meter_fx_on : white bricks (FX)
 - twarch_meter_off   : flat near-black unlit channel (gaps in 'on' show this through)."""
import numpy as np
from PIL import Image

W, H = 40, 392
TW = 'resource/twarch/'
y = np.linspace(0.0, 1.0, H)
SEG = np.asarray(Image.open('tools/_meter_on_orig.png').convert('RGBA'))[..., 3]  # original LED gaps

def bricks(stops):
    pos = [s[0] for s in stops]
    r = np.interp(y, pos, [s[1][0] for s in stops])
    g = np.interp(y, pos, [s[1][1] for s in stops])
    b = np.interp(y, pos, [s[1][2] for s in stops])
    col = np.stack([r, g, b], 1).astype(np.uint8)              # H x 3
    img = np.zeros((H, W, 4), np.uint8)
    img[..., :3] = col[:, None, :]
    img[..., 3] = SEG                                          # segmented (brick) alpha
    return Image.fromarray(img, 'RGBA')

bricks([(0.00, (255, 34, 210)), (0.32, (216, 52, 220)), (0.52, (154, 74, 226)),
        (0.70, (90, 122, 238)), (1.00, (22, 242, 222))]).save(TW + 'twarch_meter_on.png')
bricks([(0.00, (255, 255, 255)), (1.00, (228, 238, 255))]).save(TW + 'twarch_meter_fx_on.png')

off = np.zeros((H, W, 4), np.uint8); off[..., :3] = (11, 11, 20); off[..., 3] = 255
Image.fromarray(off, 'RGBA').save(TW + 'twarch_meter_off.png')
print('meters: segmented gradient on / white fx / flat near-black off')
