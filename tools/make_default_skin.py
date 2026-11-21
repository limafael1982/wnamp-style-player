#!/usr/bin/env python3
"""Generates the ORIGINAL default Winamp-2-format skin for wnamp.

Output: data/defskin/*.bmp, pledit.txt, viscolor.txt and data/default.wsz.
All artwork is drawn here from scratch (flat charcoal panels, teal/amber accents).
Sprite rectangles are parsed from src/skin/Sprites*.h so the skin always matches the code.

Usage:  python3 tools/make_default_skin.py        (needs Pillow)
"""
import os
import re
import zipfile

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIN_SRC = os.path.join(ROOT, 'src', 'skin')
OUT = os.path.join(ROOT, 'data', 'defskin')
WSZ = os.path.join(ROOT, 'data', 'default.wsz')

# --------------------------------------------------------------------------
# Parse sprite rectangles out of the C++ headers.
# --------------------------------------------------------------------------
RECT_RE = re.compile(r'\b(k[A-Za-z0-9_]+)\s*\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}')
INT_RE = re.compile(r'\b(k[A-Za-z0-9_]+)\s*=\s*(-?\d+)')  # also handles 'int kA = 1, kB = 2;'


def parse_header(name):
    txt = open(os.path.join(SKIN_SRC, name), encoding='utf-8').read()
    txt = re.sub(r'//[^\n]*', '', txt)
    m = re.search(r'namespace\s+\w+\s*\{', txt)
    body = txt[m.end():]
    rects = {n: tuple(int(v) for v in g) for n, *g in RECT_RE.findall(body)}
    ints = {n: int(v) for n, v in INT_RE.findall(body)}
    return rects, ints


M, MI = parse_header('Sprites.h')
E, EI = parse_header('SpritesEq.h')
P, PI = parse_header('SpritesPl.h')

# --------------------------------------------------------------------------
# Palette
# --------------------------------------------------------------------------
C = dict(
    bg=(30, 35, 40), bg2=(24, 28, 32), dark=(11, 15, 18), darker=(7, 10, 12),
    hi=(66, 76, 85), lo=(12, 15, 17), mid=(44, 51, 57), mid2=(54, 62, 70),
    light=(176, 187, 194), dim=(93, 107, 115), dimmer=(55, 66, 73),
    teal=(41, 211, 192), teal_d=(18, 98, 90), teal_dd=(10, 52, 49),
    amber=(255, 176, 0), amber_d=(120, 82, 0),
)

# --------------------------------------------------------------------------
# Original 3x5 pixel font used for all small text. Rows are strings of 3 bits.
# --------------------------------------------------------------------------
F35 = {
    'A': '010 101 111 101 101', 'B': '110 101 110 101 110', 'C': '011 100 100 100 011',
    'D': '110 101 101 101 110', 'E': '111 100 110 100 111', 'F': '111 100 110 100 100',
    'G': '011 100 101 101 011', 'H': '101 101 111 101 101', 'I': '111 010 010 010 111',
    'J': '001 001 001 101 010', 'K': '101 101 110 101 101', 'L': '100 100 100 100 111',
    'M': '101 111 111 101 101', 'N': '110 101 101 101 101', 'O': '010 101 101 101 010',
    'P': '110 101 110 100 100', 'Q': '010 101 101 111 011', 'R': '110 101 110 101 101',
    'S': '011 100 010 001 110', 'T': '111 010 010 010 010', 'U': '101 101 101 101 111',
    'V': '101 101 101 101 010', 'W': '101 101 111 111 101', 'X': '101 101 010 101 101',
    'Y': '101 101 010 010 010', 'Z': '111 001 010 100 111',
    '0': '111 101 101 101 111', '1': '010 110 010 010 111', '2': '110 001 010 100 111',
    '3': '110 001 010 001 110', '4': '101 101 111 001 001', '5': '111 100 110 001 110',
    '6': '011 100 111 101 111', '7': '111 001 010 010 010', '8': '111 101 111 101 111',
    '9': '111 101 111 001 110',
    '.': '000 000 000 000 010', ':': '000 010 000 010 000', '(': '001 010 010 010 001',
    ')': '100 010 010 010 100', '-': '000 000 111 000 000', "'": '010 010 000 000 000',
    '!': '010 010 010 000 010', '_': '000 000 000 000 111', '+': '000 010 111 010 000',
    '\\': '100 100 010 001 001', '/': '001 001 010 100 100', '[': '011 010 010 010 011',
    ']': '110 010 010 010 110', '^': '010 101 000 000 000', '&': '010 101 010 101 011',
    '%': '101 001 010 100 101', ',': '000 000 000 010 100', '=': '000 111 000 111 000',
    '$': '011 110 010 011 110', '#': '101 111 101 111 101', '"': '101 101 000 000 000',
    '@': '011 101 111 100 011', '?': '110 001 010 000 010', '*': '000 101 010 101 000',
    ' ': '000 000 000 000 000',
}


def text(d, x, y, s, color, scale=1, spacing=1):
    """Draw s with the 3x5 font; returns the x after the last glyph."""
    for ch in s.upper():
        rows = F35.get(ch, F35[' ']).split()
        for ry, row in enumerate(rows):
            for rx, bit in enumerate(row):
                if bit == '1':
                    px, py = x + rx * scale, y + ry * scale
                    d.rectangle([px, py, px + scale - 1, py + scale - 1], fill=color)
        x += (3 + spacing) * scale
    return x


def text_w(s, scale=1, spacing=1):
    return max(0, len(s) * (3 + spacing) * scale - spacing * scale)


# --------------------------------------------------------------------------
# Drawing helpers
# --------------------------------------------------------------------------
def new(w, h, fill):
    im = Image.new('RGB', (w, h), fill)
    return im, ImageDraw.Draw(im)


def box(d, x, y, w, h, fill=None, outline=None):
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=fill, outline=outline)


def inset(d, x, y, w, h, fill=None):
    """Recessed panel: dark fill, dark top/left, light bottom/right."""
    box(d, x, y, w, h, fill=fill or C['dark'])
    d.line([x, y, x + w - 1, y], fill=C['lo'])
    d.line([x, y, x, y + h - 1], fill=C['lo'])
    d.line([x, y + h - 1, x + w - 1, y + h - 1], fill=C['hi'])
    d.line([x + w - 1, y, x + w - 1, y + h - 1], fill=C['hi'])


def bevel_button(d, x, y, w, h, pressed=False, body=None):
    """Flat button with a 1px dark outline and a light/dark bevel."""
    box(d, x, y, w, h, fill=C['lo'])
    b = body or (C['mid'] if not pressed else C['bg2'])
    box(d, x + 1, y + 1, w - 2, h - 2, fill=b)
    hi, lo = (C['hi'], C['lo']) if not pressed else (C['lo'], C['mid2'])
    d.line([x + 1, y + 1, x + w - 2, y + 1], fill=hi)
    d.line([x + 1, y + 1, x + 1, y + h - 2], fill=hi)
    d.line([x + 1, y + h - 2, x + w - 2, y + h - 2], fill=lo)
    d.line([x + w - 2, y + 1, x + w - 2, y + h - 2], fill=lo)


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def save(im, name):
    os.makedirs(OUT, exist_ok=True)
    im.save(os.path.join(OUT, name), format='BMP')  # RGB -> 24-bit BMP


# --------------------------------------------------------------------------
# Small icons (non-antialiased primitives)
# --------------------------------------------------------------------------
def icon_play(d, x, y, c):
    d.polygon([(x, y), (x, y + 6), (x + 6, y + 3)], fill=c)


def icon_pause(d, x, y, c):
    box(d, x, y, 2, 7, fill=c)
    box(d, x + 4, y, 2, 7, fill=c)


def icon_stop(d, x, y, c):
    box(d, x, y, 7, 7, fill=c)


def icon_prev(d, x, y, c):
    box(d, x, y, 2, 7, fill=c)
    d.polygon([(x + 8, y), (x + 8, y + 6), (x + 2, y + 3)], fill=c)


def icon_next(d, x, y, c):
    box(d, x + 6, y, 2, 7, fill=c)
    d.polygon([(x, y), (x, y + 6), (x + 6, y + 3)], fill=c)


def icon_eject(d, x, y, c):
    d.polygon([(x, y + 4), (x + 8, y + 4), (x + 4, y)], fill=c)
    box(d, x, y + 6, 9, 2, fill=c)


# --------------------------------------------------------------------------
# main.bmp  275x116
# --------------------------------------------------------------------------
def make_main():
    im, d = new(275, 116, C['bg'])
    d.line([0, 0, 274, 0], fill=C['hi'])
    d.line([0, 0, 0, 115], fill=C['hi'])
    d.line([0, 115, 274, 115], fill=C['lo'])
    d.line([274, 0, 274, 115], fill=C['lo'])
    d.line([1, 1, 273, 1], fill=C['mid2'])
    # time / visualizer panel
    inset(d, 18, 19, 87, 44)
    # colon between minutes (x=48,60) and seconds (x=78,90)
    box(d, 72, 29, 2, 2, fill=C['teal'])
    box(d, 72, 34, 2, 2, fill=C['teal'])
    # visualizer area (24,43,76,16): dotted baseline
    for x in range(24, 100, 2):
        d.point((x, 58), fill=C['dimmer'])
    # info panel
    inset(d, 107, 19, 162, 35)
    text(d, 111, 34, 'WNAMP', C['teal_d'])
    text(d, 128, 43, 'KBPS', C['dim'])
    text(d, 168, 43, 'KHZ', C['dim'])
    # position bar groove (visible while stopped)
    inset(d, 16, 72, 248, 10)
    d.line([18, 76, 261, 76], fill=C['dimmer'])
    # transport panel
    inset(d, 12, 84, 148, 26, fill=C['bg2'])
    # logo
    # logo mark: a 'W' zigzag (2px strokes)
    d.line([(246, 92), (251, 106), (257, 96), (263, 106), (268, 92)], fill=C['amber_d'], width=2)
    save(im, 'main.bmp')


# --------------------------------------------------------------------------
# titlebar.bmp  344x87
# --------------------------------------------------------------------------
def title_strip(d, x, y, w, h, active, label, scale=2):
    top = C['mid2'] if active else C['mid']
    for yy in range(h):
        d.line([x, y + yy, x + w - 1, y + yy], fill=lerp(top, C['bg2'], yy / max(1, h - 1)))
    # outline rows; stacked/overlapping strips must use identical rows here
    d.line([x, y, x + w - 1, y], fill=C['lo'])
    d.line([x, y + h - 1, x + w - 1, y + h - 1], fill=C['lo'])
    tw = text_w(label, scale)
    tx = x + (w - tw) // 2
    ty = y + (h - 5 * scale) // 2
    text(d, tx, ty, label, C['amber'] if active else C['dim'], scale)
    gl = C['teal_d'] if active else C['dimmer']
    for k in (-2, 2):
        d.line([x + 22, y + h // 2 + k, tx - 6, y + h // 2 + k], fill=gl)
        d.line([tx + tw + 6, y + h // 2 + k, x + w - 40, y + h // 2 + k], fill=gl)


def small_button(d, x, y, kind, pressed):
    box(d, x, y, 9, 9, fill=C['lo'])
    box(d, x + 1, y + 1, 7, 7, fill=C['mid'] if not pressed else C['dark'])
    if not pressed:
        d.line([x + 1, y + 1, x + 7, y + 1], fill=C['hi'])
        d.line([x + 1, y + 1, x + 1, y + 7], fill=C['hi'])
    g = C['light'] if not pressed else C['teal']
    if kind == 'menu':
        for yy in (2, 4, 6):
            d.line([x + 2, y + yy, x + 6, y + yy], fill=g)
    elif kind == 'min':
        d.line([x + 2, y + 6, x + 6, y + 6], fill=g)
    elif kind == 'shade':
        box(d, x + 2, y + 2, 5, 2, fill=g)
        d.line([x + 2, y + 6, x + 6, y + 6], fill=C['dim'] if not pressed else g)
    elif kind == 'unshade':
        box(d, x + 2, y + 2, 5, 5, outline=g)
    elif kind == 'close':
        for k in range(5):
            d.point((x + 2 + k, y + 2 + k), fill=g)
            d.point((x + 6 - k, y + 2 + k), fill=g)


def make_titlebar():
    im, d = new(344, 87, C['bg2'])
    title_strip(d, *M['kTitleActive'], True, 'WNAMP')
    title_strip(d, *M['kTitleInactive'], False, 'WNAMP')
    # kTitleShadeInactive / kTitleShadeActive overlap by one row; both strips draw the same
    # outline colour there, so draw inactive first, active second.
    title_strip(d, *M['kTitleShadeInactive'], False, 'WNAMP', scale=1)
    title_strip(d, *M['kTitleShadeActive'], True, 'WNAMP', scale=1)
    for up, down, kind in (('kMenuUp', 'kMenuDown', 'menu'), ('kMinimizeUp', 'kMinimizeDown', 'min'),
                           ('kShadeUp', 'kShadeDown', 'shade'), ('kCloseUp', 'kCloseDown', 'close'),
                           ('kUnshadeUp', 'kUnshadeDown', 'unshade')):
        for nm, pr in ((up, False), (down, True)):
            x, y, _, _ = M[nm]
            small_button(d, x, y, kind, pr)
    save(im, 'titlebar.bmp')


# --------------------------------------------------------------------------
# cbuttons.bmp  136x36
# --------------------------------------------------------------------------
def make_cbuttons():
    im, d = new(136, 36, C['lo'])
    items = (('Prev', icon_prev, 8, 7), ('Play', icon_play, 7, 7), ('Pause', icon_pause, 6, 7),
             ('Stop', icon_stop, 7, 7), ('Next', icon_next, 8, 7), ('Eject', icon_eject, 9, 8))
    for name, fn, iw, ih in items:
        for state in ('Up', 'Down'):
            x, y, w, h = M['k%s%s' % (name, state)]
            pressed = state == 'Down'
            bevel_button(d, x, y, w, h, pressed)
            ix = x + (w - iw) // 2 + (1 if pressed else 0)
            iy = y + (h - ih) // 2 + (1 if pressed else 0)
            col = C['teal'] if name == 'Play' else (C['amber'] if pressed else C['light'])
            fn(d, ix, iy, col)
    # column 114 is shared by Next's right edge and Eject's left edge: outline colour
    d.line([114, 0, 114, 35], fill=C['lo'])
    save(im, 'cbuttons.bmp')


# --------------------------------------------------------------------------
# numbers.bmp  99x13   (7-segment digits 9x13, plus one blank cell)
# --------------------------------------------------------------------------
SEG = {'0': 'abcdef', '1': 'bc', '2': 'abged', '3': 'abgcd', '4': 'fgbc',
       '5': 'afgcd', '6': 'afgedc', '7': 'abc', '8': 'abcdefg', '9': 'abcdfg'}


def draw_digit(d, x0, ch):
    on, off = C['teal'], (14, 38, 40)
    segs = {'a': (1, 0, 7, 2), 'g': (1, 5, 7, 2), 'd': (1, 11, 7, 2),
            'f': (0, 2, 2, 3), 'b': (7, 2, 2, 3), 'e': (0, 7, 2, 4), 'c': (7, 7, 2, 4)}
    lit = SEG.get(ch, '')
    for s, (sx, sy, sw, sh) in segs.items():
        box(d, x0 + sx, sy, sw, sh, fill=on if s in lit else off)


def make_numbers():
    im, d = new(99, 13, C['dark'])
    for i in range(10):
        draw_digit(d, i * 9, str(i))
    save(im, 'numbers.bmp')


# --------------------------------------------------------------------------
# text.bmp  155x18   (5x6 glyph cells; 31 columns x 3 rows, same mapping as Sprites.h glyph())
# --------------------------------------------------------------------------
def make_text():
    fg = C['amber']
    im, d = new(155, 18, C['dark'])

    def cell(col, row, rows35, dy=0):
        for ry, r in enumerate(rows35.split()):
            for rx, bit in enumerate(r):
                if bit == '1':
                    d.point((col * 5 + 1 + rx, row * 6 + dy + ry), fill=fg)

    for i, ch in enumerate('ABCDEFGHIJKLMNOPQRSTUVWXYZ'):
        cell(i, 0, F35[ch])
    cell(26, 0, F35['"'])
    cell(27, 0, F35['@'])
    for i, ch in enumerate('0123456789'):
        cell(i, 1, F35[ch])
    for x in (0, 2, 4):  # col 10: ellipsis stand-in
        d.point((10 * 5 + x, 6 + 4), fill=fg)
    for i, ch in enumerate(".:()-'!_+\\/[]^&%,=$#"):  # cols 11..30
        cell(11 + i, 1, F35[ch])
    cell(0, 2, F35['A'], dy=1)   # A-ring
    cell(0, 2, '010')
    cell(1, 2, F35['O'], dy=1)   # O-umlaut
    cell(1, 2, '101')
    cell(2, 2, F35['A'], dy=1)   # A-umlaut
    cell(2, 2, '101')
    cell(3, 2, F35['?'])
    cell(4, 2, F35['*'])
    save(im, 'text.bmp')


# --------------------------------------------------------------------------
# posbar.bmp  307x10
# --------------------------------------------------------------------------
def make_posbar():
    im, d = new(307, 10, C['bg'])
    x, y, w, h = M['kPosBarTrack']
    inset(d, x, y, w, h)
    d.line([x + 2, y + 4, x + w - 3, y + 4], fill=C['dimmer'])
    d.line([x + 2, y + 5, x + w - 3, y + 5], fill=C['dimmer'])
    for nm, pressed in (('kPosThumbUp', False), ('kPosThumbDown', True)):
        x, y, w, h = M[nm]
        bevel_button(d, x, y, w, h, pressed, body=C['mid2'] if not pressed else C['teal_d'])
        box(d, x + w // 2 - 3, y + 2, 6, h - 4, fill=C['teal'] if pressed else C['amber'])
    save(im, 'posbar.bmp')


# --------------------------------------------------------------------------
# volume.bmp / balance.bmp  (68x433)
# --------------------------------------------------------------------------
def slider_thumb(d, x, y, w, h, pressed):
    bevel_button(d, x, y, w, h, pressed, body=C['mid2'] if not pressed else C['teal_d'])
    box(d, x + w // 2 - 1, y + 2, 2, h - 4, fill=C['teal'] if pressed else C['amber'])


def make_volume(name, balance):
    im, d = new(68, 433, C['bg2'])
    frames, pitch, tw = MI['kSliderFrames'], MI['kSliderFramePitch'], MI['kSliderThumbW']
    vis_w = 38 if balance else 68
    x0 = MI['kBalanceSrcX'] if balance else 0
    for f in range(frames):
        y = f * pitch
        t = f / (frames - 1)
        box(d, x0, y, vis_w, 13, fill=C['darker'])
        d.line([x0, y, x0 + vis_w - 1, y], fill=C['lo'])
        d.line([x0, y + 12, x0 + vis_w - 1, y + 12], fill=C['hi'])
        box(d, x0 + 2, y + 5, vis_w - 4, 3, fill=C['dimmer'])
        travel = vis_w - tw
        if balance:
            # frame index = |balance|; fill grows symmetrically out from the centre
            centre = x0 + vis_w // 2
            half = int(round(t * travel / 2)) + tw // 2
            box(d, centre - half, y + 5, 2 * half, 3, fill=lerp(C['teal_d'], C['teal'], t))
            d.line([centre, y + 2, centre, y + 10], fill=C['light'])
        else:
            fill_w = int(round(t * travel)) + tw // 2
            if fill_w > 0:
                box(d, x0 + 2, y + 5, min(fill_w, vis_w - 4), 3, fill=lerp(C['teal_d'], C['amber'], t))
    for nm, pressed in (('kSliderThumbUp', False), ('kSliderThumbDown', True)):
        x, y, w, h = M[nm]
        slider_thumb(d, x, y, w, h, pressed)
    save(im, name)


# --------------------------------------------------------------------------
# shufrep.bmp  92x85
# --------------------------------------------------------------------------
def toggle_sprite(d, rect, label, on, pressed):
    x, y, w, h = rect
    if on:
        body, col = (C['teal_d'] if not pressed else C['teal_dd']), C['teal']
    else:
        body, col = (C['mid'] if not pressed else C['bg2']), (C['dim'] if not pressed else C['dimmer'])
    bevel_button(d, x, y, w, h, pressed, body=body)
    o = 1 if pressed else 0
    text(d, x + (w - text_w(label)) // 2 + o, y + (h - 5) // 2 + o, label, col)


def make_shufrep():
    im, d = new(92, 85, C['bg2'])
    for base, label in (('Repeat', 'REPEAT'), ('Shuffle', 'SHUFFLE'), ('Eq', 'EQ'), ('Pl', 'PL')):
        for nm, on, pr in ((base + 'Up', False, False), (base + 'Down', False, True),
                           (base + 'OnUp', True, False), (base + 'OnDown', True, True)):
            toggle_sprite(d, M['k' + nm], label, on, pr)
    save(im, 'shufrep.bmp')


# --------------------------------------------------------------------------
# playpaus.bmp 42x9, monoster.bmp 56x24
# --------------------------------------------------------------------------
def make_playpaus():
    im, d = new(42, 9, C['dark'])
    x, y, _, _ = M['kStatePlaying']
    icon_play(d, x + 1, y + 1, C['teal'])
    x, y, _, _ = M['kStatePaused']
    icon_pause(d, x + 1, y + 1, C['amber'])
    x, y, _, _ = M['kStateStopped']
    icon_stop(d, x + 1, y + 1, C['dim'])
    save(im, 'playpaus.bmp')


def make_monoster():
    im, d = new(56, 24, C['dark'])
    for nm, label, on in (('kStereoOn', 'STEREO', True), ('kStereoOff', 'STEREO', False),
                          ('kMonoOn', 'MONO', True), ('kMonoOff', 'MONO', False)):
        x, y, w, h = M[nm]
        box(d, x, y, w, h, fill=C['darker'])
        text(d, x + (w - text_w(label)) // 2, y + (h - 5) // 2, label, C['teal'] if on else C['dimmer'])
    save(im, 'monoster.bmp')


# --------------------------------------------------------------------------
# eqmain.bmp  275x315
# --------------------------------------------------------------------------
EQ_LABELS = ['29', '59', '119', '237', '474', '947', '1.9K', '3.8K', '7.5K', '15K']  # GStreamer band centres


def make_eq():
    im, d = new(275, 315, C['bg2'])
    box(d, 0, 0, 275, 116, fill=C['bg'])
    d.line([0, 0, 274, 0], fill=C['hi'])
    d.line([0, 0, 0, 115], fill=C['hi'])
    d.line([0, 115, 274, 115], fill=C['lo'])
    d.line([274, 0, 274, 115], fill=C['lo'])
    # response-curve box and PRESETS button (classic EQ layout positions)
    inset(d, 86, 17, 113, 19)
    for x in range(88, 197, 3):
        d.point((x, 26), fill=C['dimmer'])
    bevel_button(d, 217, 18, 44, 12)
    text(d, 217 + (44 - text_w('PRESETS')) // 2, 22, 'PRESETS', C['light'])
    # scale labels, centre guide, band labels
    for yy, lab, lab2 in ((39, '+12', '+12DB'), (67, '0', '0DB'), (95, '-12', '-12DB')):
        text(d, 3, yy, lab, C['dim'])
        text(d, 44, yy, lab2, C['dimmer'])
    for x in range(40, 76, 2):
        d.point((x, 69), fill=C['dimmer'])
    text(d, 14, 104, 'PREAMP', C['dim'])
    bx0, pitch = EI['kBandX0'], EI['kBandPitch']
    for i, lab in enumerate(EQ_LABELS):
        text(d, bx0 + i * pitch - 1, 104, lab, C['dim'])
    title_strip(d, *E['kTitleActive'], True, 'EQUALIZER')
    title_strip(d, *E['kTitleInactive'], False, 'EQUALIZER')
    for nm, pr in (('kCloseUp', False), ('kCloseDown', True)):
        x, y, _, _ = E[nm]
        small_button(d, x, y, 'close', pr)
    for base, label in (('On', 'ON'), ('Auto', 'AUTO')):
        for nm, on, pr in ((base + 'OffUp', False, False), (base + 'OffDown', False, True),
                           (base + 'OnUp', True, False), (base + 'OnDown', True, True)):
            toggle_sprite(d, E['k' + nm], label, on, pr)
    # slider frames: 28 frames; fill from centre to thumb (teal = boost, amber = cut)
    fc, sw, sh = EI['kFrameCount'], EI['kSliderW'], EI['kSliderH']
    th, cols = EI['kThumbH'], EI['kFrameColumns']
    travel = sh - th
    for f in range(fc):
        fx = 13 + (f % cols) * 15
        fy = 164 + (f // cols) * 65
        n = f / (fc - 1)
        box(d, fx, fy, sw, sh, fill=C['darker'])
        d.line([fx, fy, fx + sw - 1, fy], fill=C['lo'])
        for ty in (th // 2, sh // 2, sh - 1 - th // 2):
            d.line([fx + 1, fy + ty, fx + 3, fy + ty], fill=C['dim'])
            d.line([fx + sw - 4, fy + ty, fx + sw - 2, fy + ty], fill=C['dim'])
        cx = fx + sw // 2 - 1
        box(d, cx, fy + 2, 2, sh - 4, fill=C['dimmer'])
        centre = sh // 2
        tc = int(round(n * travel)) + th // 2
        if tc < centre:
            box(d, cx, fy + tc, 2, centre - tc + 1, fill=C['teal'])
        elif tc > centre:
            box(d, cx, fy + centre, 2, tc - centre + 1, fill=C['amber'])
    for nm, pr in (('kThumbUp', False), ('kThumbDown', True)):
        x, y, w, h = E[nm]
        bevel_button(d, x, y, w, h, pr, body=C['mid2'] if not pr else C['teal_d'])
        d.line([x + 2, y + h // 2, x + w - 3, y + h // 2], fill=C['teal'] if pr else C['amber'])
    save(im, 'eqmain.bmp')


# --------------------------------------------------------------------------
# pledit.bmp  280x186
# --------------------------------------------------------------------------
P_BTN_W, P_BTN_H = PI['kBtnW'], PI['kBtnH']


def pl_button(d, x, y, label):
    bevel_button(d, x, y, P_BTN_W, P_BTN_H)
    text(d, x + (P_BTN_W - text_w(label)) // 2, y + (P_BTN_H - 5) // 2, label, C['light'])


def pl_top(d, active):
    suffix = 'Active' if active else 'Inactive'
    top = C['mid2'] if active else C['mid']
    for nm, kind in (('kTopLeft', 'left'), ('kTitle', 'title'), ('kTopTile', 'tile'), ('kTopRight', 'right')):
        x, y, w, h = P[nm + suffix]
        for yy in range(h):
            d.line([x, y + yy, x + w - 1, y + yy], fill=lerp(top, C['bg2'], yy / (h - 1)))
        d.line([x, y, x + w - 1, y], fill=C['lo'])
        d.line([x, y + h - 1, x + w - 1, y + h - 1], fill=C['lo'])
        if kind == 'left':
            d.line([x, y, x, y + h - 1], fill=C['lo'])
            d.line([x + 1, y + 1, x + 1, y + h - 2], fill=C['hi'])
        elif kind == 'right':
            d.line([x + w - 1, y, x + w - 1, y + h - 1], fill=C['lo'])
            # close button baked into the frame: 9x9, 11 px from the right edge, 3 px down
            small_button(d, x + w - PI['kCloseFromRight'], y + PI['kCloseY'], 'close', False)
        elif kind == 'title':
            label = 'PLAYLIST'
            text(d, x + (w - text_w(label, 2)) // 2, y + 5, label, C['amber'] if active else C['dim'], 2)
        else:
            gl = C['teal_d'] if active else C['dimmer']
            d.line([x, y + 8, x + w - 1, y + 8], fill=gl)
            d.line([x, y + 12, x + w - 1, y + 12], fill=gl)


def make_pledit():
    im, d = new(280, 186, C['bg2'])
    pl_top(d, True)
    pl_top(d, False)
    x, y, w, h = P['kLeftTile']
    box(d, x, y, w, h, fill=C['bg'])
    d.line([x, y, x, y + h - 1], fill=C['hi'])
    d.line([x + w - 2, y, x + w - 2, y + h - 1], fill=C['lo'])
    d.line([x + w - 1, y, x + w - 1, y + h - 1], fill=C['hi'])
    x, y, w, h = P['kRightTile']
    box(d, x, y, w, h, fill=C['bg'])
    d.line([x, y, x, y + h - 1], fill=C['lo'])
    d.line([x + 1, y, x + 1, y + h - 1], fill=C['hi'])
    box(d, x + 5, y, 10, h, fill=C['darker'])           # scrollbar track
    d.line([x + 9, y, x + 9, y + h - 1], fill=C['dimmer'])
    d.line([x + w - 1, y, x + w - 1, y + h - 1], fill=C['lo'])
    for nm in ('kBottomLeft', 'kBottomRight', 'kBottomTile'):
        x, y, w, h = P[nm]
        box(d, x, y, w, h, fill=C['bg'])
        d.line([x, y, x + w - 1, y], fill=C['hi'])
        d.line([x, y + h - 1, x + w - 1, y + h - 1], fill=C['lo'])
    x, y, w, h = P['kBottomLeft']
    d.line([x, y, x, y + h - 1], fill=C['hi'])
    by = y + h - PI['kBtnFromBottom']
    for bx, lab in ((PI['kAddX'], 'ADD'), (PI['kRemX'], 'REM'), (PI['kSelX'], 'SEL'), (PI['kMiscX'], 'MISC')):
        pl_button(d, x + bx, by, lab)
    x, y, w, h = P['kBottomRight']
    d.line([x + w - 1, y, x + w - 1, y + h - 1], fill=C['lo'])
    inset(d, x + 8, y + 8, 96, 18, fill=C['bg2'])        # mini transport strip
    for i, fn in enumerate((icon_prev, icon_play, icon_pause, icon_stop, icon_next)):
        fn(d, x + 14 + i * 14, y + 14, C['dim'])
    pl_button(d, x + 112, y + 8, 'LIST')                 # LIST button, piece-local x=112
    save(im, 'pledit.bmp')


# --------------------------------------------------------------------------
# pledit.txt / viscolor.txt
# --------------------------------------------------------------------------
def make_text_files():
    with open(os.path.join(OUT, 'pledit.txt'), 'w', newline='\r\n') as f:
        f.write('[Text]\nNormal=#3FE0C5\nCurrent=#FFB000\nNormalBG=#0B1114\nSelectedBG=#1D3A44\nFont=Arial\n')
    cols = [(0, 0, 0), (28, 38, 44)]
    for i in range(16):  # 2..17: spectrum gradient, bottom (teal) to top (amber)
        cols.append(lerp(C['teal'], C['amber'], i / 15))
    for i in range(5):   # 18..22: oscilloscope
        cols.append(lerp((24, 140, 130), (200, 240, 235), i / 4))
    cols.append((255, 255, 255))  # 23: peak marker
    assert len(cols) == 24
    with open(os.path.join(OUT, 'viscolor.txt'), 'w', newline='\r\n') as f:
        for r, g, b in cols:
            f.write('%d,%d,%d\n' % (r, g, b))


EXPECT = {
    'main.bmp': (275, 116), 'titlebar.bmp': (344, 87), 'cbuttons.bmp': (136, 36),
    'numbers.bmp': (99, 13), 'text.bmp': (155, 18), 'posbar.bmp': (307, 10),
    'volume.bmp': (68, 433), 'balance.bmp': (68, 433), 'shufrep.bmp': (92, 85),
    'playpaus.bmp': (42, 9), 'monoster.bmp': (56, 24), 'eqmain.bmp': (275, 315),
    'pledit.bmp': (280, 186),
}


def main():
    os.makedirs(OUT, exist_ok=True)
    make_main(); make_titlebar(); make_cbuttons(); make_numbers(); make_text()
    make_posbar(); make_volume('volume.bmp', False); make_volume('balance.bmp', True)
    make_shufrep(); make_playpaus(); make_monoster(); make_eq(); make_pledit()
    make_text_files()
    names = sorted(list(EXPECT) + ['pledit.txt', 'viscolor.txt'])
    for n, size in EXPECT.items():
        with Image.open(os.path.join(OUT, n)) as im:
            assert im.size == size, (n, im.size)
    with zipfile.ZipFile(WSZ, 'w', zipfile.ZIP_DEFLATED) as z:
        for n in names:
            z.write(os.path.join(OUT, n), n)
    print('wrote', len(names), 'files to', OUT, 'and', WSZ)


if __name__ == '__main__':
    main()
