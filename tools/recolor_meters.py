"""Regenerate the Twarch VU-meter bitmaps (40x392, revealed bottom-up by CVuMeter):
 - twarch_meter_on    : smooth magenta(top/loud) -> purple -> cyan(bottom/quiet) gradient (IN/OUT)
 - twarch_meter_fx_on : white (FX meter)
 - twarch_meter_off   : flat near-black unlit channel
All fully opaque & smooth (no segment gaps); num-led=25 only quantizes fill height."""
import numpy as np
from PIL import Image

W, H = 40, 392
TW = 'resource/twarch/'
y = np.linspace(0.0, 1.0, H)   # 0 = top (loud), 1 = bottom (quiet)

def vgrad(stops):
    pos = [s[0] for s in stops]
    r = np.interp(y, pos, [s[1][0] for s in stops])
    g = np.interp(y, pos, [s[1][1] for s in stops])
    b = np.interp(y, pos, [s[1][2] for s in stops])
    col = np.stack([r, g, b], 1).astype(np.uint8)          # H x 3
    img = np.zeros((H, W, 4), np.uint8)
    img[..., :3] = col[:, None, :]
    img[..., 3] = 255
    return Image.fromarray(img, 'RGBA')

# magenta -> purple -> blue -> cyan (matches the reference meter)
on = vgrad([(0.00, (255, 34, 210)), (0.32, (216, 52, 220)), (0.52, (154, 74, 226)),
            (0.70, (90, 122, 238)), (1.00, (22, 242, 222))])
on.save(TW + 'twarch_meter_on.png')

fx = vgrad([(0.00, (255, 255, 255)), (1.00, (228, 238, 255))])   # white
fx.save(TW + 'twarch_meter_fx_on.png')

off = vgrad([(0.0, (12, 12, 22)), (1.0, (10, 10, 18))])          # near-black unlit
off.save(TW + 'twarch_meter_off.png')
print('meters written: on(gradient) fx(white) off(near-black)')
