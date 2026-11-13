# BC candidates from 20 kHz zone shots (frames 1921x1440, pan x 18..1378, centre = the shot's
# frequency): steady narrow carriers and wide voice bands in the waterfall's newest rows; a
# candidate is an LSB voice band whose 2.9 kHz passband (dial = its top edge + 0.25 kHz) holds
# a carrier 0.3-2.7 kHz below the dial (an audible whistle over the voice).
import subprocess, sys, os, re
X0, X1, Y0, Y1 = 18, 1378, 815, 1075
def analyse(png, centre):
    w, h = X1 - X0, Y1 - Y0
    raw = subprocess.check_output(['magick', png, '-crop', f'{w}x{h}+{X0}+{Y0}', '+repage', '-depth', '8', 'rgb:-'])
    hz_px = 20000 / w
    lit = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 3
            lit[y][x] = raw[i] + raw[i + 1]          # the palette's blue floor has little red / green
    col_mean = [sum(lit[y][x] for y in range(h)) / h for x in range(w)]
    col_on = [sum(1 for y in range(h) if lit[y][x] > 150) / h for x in range(w)]
    floor = sorted(col_mean)[w // 4]
    carriers = []
    x = 0
    while x < w:
        if col_on[x] > 0.75 and col_mean[x] > floor + 120:
            x2 = x
            while x2 + 1 < w and col_on[x2 + 1] > 0.75:
                x2 += 1
            if x2 - x <= 8:
                carriers.append((x + x2) / 2)
            x = x2 + 1
        else:
            x += 1
    voiced = [0.08 < col_on[x] < 0.75 or (col_mean[x] > floor + 40 and col_on[x] < 0.75) for x in range(w)]
    bands = []
    x = 0
    while x < w:
        if voiced[x]:
            x2 = x
            gap = 0
            while x2 + 1 < w and (voiced[x2 + 1] or gap < 6):
                gap = 0 if voiced[x2 + 1] else gap + 1
                x2 += 1
            if (x2 - x) * hz_px >= 1500:
                bands.append((x, x2))
            x = x2 + 1
        else:
            x += 1
    f = lambda px: centre - 0.010 + px * hz_px / 1e6
    out = []
    for lo, hi in bands:
        dial = f(hi) + 0.00025
        for c in carriers:
            off = (dial - f(c)) * 1e3
            if 0.3 <= off <= 2.7:
                out.append((round(dial, 4), round(f(c), 4), round(off, 2)))
    # the passband's edge lines and the dial (LSB, 2.7 or 2.9 kHz) are drawn, not carriers
    edges = [centre - 0.0030, centre - 0.0028, centre - 0.0001, centre]
    carriers = [c for c in carriers if all(abs(f(c) - e) > 0.00006 for e in edges)]
    out = [o for o in out if all(abs(o[1] - e) > 0.00006 for e in edges)]
    # FT8 / FT4 and the digital parts are not phone: 80 m 3.570-3.600, 40 m 7.040-7.080
    out = [o for o in out if not (3.570 <= o[0] <= 3.600 or 7.040 <= o[0] <= 7.080)]
    return [round(f(c), 4) for c in carriers], [(round(f(a), 4), round(f(b), 4)) for a, b in bands], out
for d in sys.argv[1:]:
    for name in sorted(os.listdir(d)):
        m = re.match(r'z([\d.]+)\.png$', name)
        if not m:
            continue
        carriers, bands, cands = analyse(os.path.join(d, name), float(m.group(1)))
        print(name, 'carriers', carriers, 'voice', bands, 'BC' if cands else '', cands)
