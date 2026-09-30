#!/usr/bin/env python3
"""Skalenerkennung V2 (offline, 16.09.): Skalenbogen, Teilstriche, Hauptstriche und Ziffern eindeutig finden und einzeichnen.
Reihenfolge nach Nutzervorgabe: (1) unterer schwarzer Bogen als Kreis, (2) Teilstriche radial vom Bogen nach außen mit
gleichmäßigem Abstand, (3) Ziffern als dunkle Kästen hinter den Hauptstrichen. Erst danach wird der Zeiger gesucht (nicht hier).
Aufruf: python3 skala_v2.py BILD.jpg [AUSGABE.jpg]   → Kennzahlen als JSON auf stdout, Bild mit Einzeichnungen."""
import sys, json, numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage
import zeiger_lesen as zl
rng = np.random.default_rng(1)

def bogen_finden(d, B, W, H):
    """Skalenbogen: größte zusammenhängende dünne dunkle Struktur mit großer Breite → RANSAC-Kreis (Toleranz 1,5 px)."""
    lab, n = ndimage.label(B); best = None
    for i in range(1, n + 1):
        ys, xs = np.nonzero(lab == i)
        if len(xs) < 300: continue
        breite = xs.max() - xs.min()
        if breite < 0.3 * W: continue
        if best is None or len(xs) > best[0]: best = (len(xs), i, xs, ys)
    if best is None: return None
    _, i, xs, ys = best; P = np.stack([xs, ys], 1).astype(float); bx, bn = None, 0
    for _ in range(1500):
        k = rng.choice(len(P), 3, replace=False); (ax, ay), (bx_, by), (qx, qy) = P[k]
        det = 2 * (ax * (by - qy) + bx_ * (qy - ay) + qx * (ay - by))
        if abs(det) < 1e-6: continue
        ux = ((ax*ax + ay*ay) * (by - qy) + (bx_*bx_ + by*by) * (qy - ay) + (qx*qx + qy*qy) * (ay - by)) / det
        uy = ((ax*ax + ay*ay) * (qx - bx_) + (bx_*bx_ + by*by) * (ax - qx) + (qx*qx + qy*qy) * (bx_ - ax)) / det
        r = np.hypot(ax - ux, ay - uy)
        if not (150 <= r <= 500) or uy < ys.max(): continue          # Mittelpunkt unterhalb des Bogens
        inl = np.abs(np.hypot(P[:, 0] - ux, P[:, 1] - uy) - r) < 1.5; m = int(inl.sum())
        if m > bn: bn, bx = m, (ux, uy, r)
    if bx is None: return None
    for _ in range(2):                                                 # LSQ-Verfeinerung auf Inliern
        ux, uy, r = bx; inl = np.abs(np.hypot(P[:, 0] - ux, P[:, 1] - uy) - r) < 1.5
        x, y = P[inl, 0], P[inl, 1]; A = np.column_stack([x, y, np.ones(len(x))]); s = np.linalg.lstsq(A, -(x*x + y*y), rcond=None)[0]
        ux, uy = -s[0] / 2, -s[1] / 2; r = np.sqrt(ux*ux + uy*uy - s[2]); bx = (ux, uy, r)
    ux, uy, r = bx; inl = np.abs(np.hypot(P[:, 0] - ux, P[:, 1] - uy) - r) < 1.5
    ang = np.degrees(np.arctan2(-(P[inl, 1] - uy), P[inl, 0] - ux))
    return dict(cx=ux, cy=uy, r=r, inlier=int(inl.sum()), punkte=len(P), a0=float(np.percentile(ang, 1)), a1=float(np.percentile(ang, 99)), komponente=i)

def striche_finden(d, g, geo, W, H):
    d = zl.duenne_dunkle_linien(g, 5)                                          # feiner Kern: Strichabstand ≈ 9 px, ein 9er-Kern schmiert die Lücken zu
    """Teilstriche: radial vom Bogen nach außen (r+3 … r+45). Je Winkel (0,1°) Dunkelheit der dünnen Struktur direkt außen am
    Bogen; Läufe über der Schwelle = Striche; Länge des Laufs radial = Strichlänge (Hauptstriche länger)."""
    cx, cy, r = geo['cx'], geo['cy'], geo['r']; w = np.arange(geo['a0'] - 2, geo['a1'] + 2, 0.1); th = np.radians(w)
    def probe(rr): x = np.clip((cx + rr * np.cos(th)).astype(int), 0, W - 1); y = np.clip((cy - rr * np.sin(th)).astype(int), 0, H - 1); return d[y, x]
    nah = np.mean([probe(rr) for rr in (r + 5, r + 7, r + 9, r + 11)], axis=0)   # Dunkelheit außen am Bogen, außerhalb des Bogens selbst
    # Glätten (0,3°) und Spitzen suchen: lokales Maximum, Abstand >= 0,8°, Höhe >= 25 % der 99-%-Spitze und >= 10
    kern = np.ones(3) / 3; glatt = np.convolve(nah, kern, mode='same'); sw = max(10.0, 0.25 * np.percentile(glatt, 99))
    striche = []
    i = 1
    while i < len(glatt) - 1:
        if glatt[i] >= sw and glatt[i] >= glatt[i - 1] and glatt[i] >= glatt[i + 1]:
            j = i
            while j + 1 < len(glatt) and glatt[j + 1] == glatt[i]: j += 1                  # Plateau
            mitte = 0.5 * (w[i] + w[j]); t = np.radians(mitte); laenge = 0
            for rr in np.arange(r + 3, r + 60, 1.0):
                x = int(np.clip(cx + rr * np.cos(t), 0, W - 1)); y = int(np.clip(cy - rr * np.sin(t), 0, H - 1))
                if d[y, x] > sw * 0.6: laenge = rr - r
                elif rr - r > laenge + 3: break
            if not striche or abs(striche[-1][0] - mitte) >= 0.6: striche.append((mitte, laenge))
            elif laenge > striche[-1][1]: striche[-1] = (mitte, laenge)
            i = j + 6                                                                       # 0,6° Sperre
        else: i += 1
    if len(striche) < 5: return dict(striche=[], haupt=[], pitch=None, regel=None, schwelle=sw)
    ang = np.array([s[0] for s in striche]); la = np.array([s[1] for s in striche])
    lmed = np.median(la); haupt = la > 1.5 * lmed
    diff = np.abs(np.diff(ang)); pitch = float(np.median(diff)); regel = float(np.std(diff) / pitch) if pitch > 0 else None   # 44C2: 26 Striche, 5 Teilungen je 0,2 mA → 3,56°
    return dict(striche=[(float(a), float(l)) for a, l in striche], haupt=[float(a) for a in ang[haupt]], pitch=pitch, regel=regel, laenge_klein=float(lmed), schwelle=float(sw))

def ziffern_finden(g, geo, haupt, W, H):
    """Ziffern: dunkle Flecken im Ring r+40 … r+100 nahe den Hauptstrichen (±8°), als Kästen."""
    cx, cy, r = geo['cx'], geo['cy'], geo['r']; yy, xx = np.mgrid[0:H, 0:W]; rr = np.hypot(xx - cx, yy - cy); ang = np.degrees(np.arctan2(-(yy - cy), xx - cx))
    ring = (rr > r + 32) & (rr < r + 115)
    if ring.sum() == 0: return []
    weiss = np.percentile(g[ring], 90); dunkel = (g < weiss - 0.28 * weiss) & ring
    lab, n = ndimage.label(dunkel); kaesten = []
    for i in range(1, n + 1):
        ys, xs = np.nonzero(lab == i)
        if len(xs) < 25: continue
        h = ys.max() - ys.min() + 1; b = xs.max() - xs.min() + 1
        if not (8 <= h <= 45 and 4 <= b <= 60): continue
        mx, my = xs.mean(), ys.mean(); a = np.degrees(np.arctan2(-(my - cy), mx - cx))
        kaesten.append(dict(x0=int(xs.min()), y0=int(ys.min()), x1=int(xs.max()), y1=int(ys.max()), winkel=float(a), r=float(np.hypot(mx - cx, my - cy)), pixel=int(len(xs))))
    # Kästen zu Ziffern gruppieren: nahe beieinander (gleicher Winkelbereich ±3°) → eine Zahl
    kaesten.sort(key=lambda k: -k['winkel']); gruppen = []
    for k in kaesten:
        if gruppen and abs(gruppen[-1]['winkel'] - k['winkel']) < 5.0 and abs(gruppen[-1]['r'] - k['r']) < 35:
            gz = gruppen[-1]; gz.update(x0=min(gz['x0'], k['x0']), y0=min(gz['y0'], k['y0']), x1=max(gz['x1'], k['x1']), y1=max(gz['y1'], k['y1']), pixel=gz['pixel'] + k['pixel']); gz['winkel'] = 0.5 * (gz['winkel'] + k['winkel'])
        else: gruppen.append(dict(k))
    for gz in gruppen: gz['hauptstrich'] = bool(haupt) and float(min(abs(gz['winkel'] - h) for h in haupt)) < 9
    # Aufräumen: je Hauptstrich höchstens eine Zahl (die pixelreichste im Umkreis), alles andere ist keine Ziffer der Skala
    behalten = []
    for h in haupt:
        nahe = [gz for gz in gruppen if abs(gz['winkel'] - h) < 9]
        if nahe: behalten.append(max(nahe, key=lambda k: k['pixel']))
    for gz in behalten: gz['hauptstrich'] = True
    return behalten

def erkennen(pfad, aus=None):
    im = Image.open(pfad).convert('RGB'); g = np.asarray(im.convert('L')).astype(np.float32); H, W = g.shape
    d = zl.duenne_dunkle_linien(g, 9); sw = max(15.0, 0.3 * np.percentile(d, 99.5)); B = d > sw
    geo = bogen_finden(d, B, W, H)
    erg = dict(bild=str(pfad), bogen=geo)
    if geo:
        st = striche_finden(d, g, geo, W, H); erg['striche'] = st
        zf = ziffern_finden(g, geo, st['haupt'], W, H); erg['ziffern'] = zf
        erg['zusammenfassung'] = dict(bogen_r=round(geo['r'], 1), mittelpunkt=(round(geo['cx'], 1), round(geo['cy'], 1)), bogen_inlier=geo['inlier'], winkel=(round(geo['a0'], 1), round(geo['a1'], 1)),
                                      striche=len(st['striche']), hauptstriche=len(st['haupt']), pitch_grad=round(st['pitch'], 3) if st['pitch'] else None, regelmaessigkeit=round(st['regel'], 3) if st['regel'] else None,
                                      ziffern=len(zf), ziffern_an_hauptstrichen=sum(1 for z in zf if z['hauptstrich']))
    if aus:
        z = ImageDraw.Draw(im)
        if geo:
            cx, cy, r = geo['cx'], geo['cy'], geo['r']
            pts = [(cx + r * np.cos(np.radians(a)), cy - r * np.sin(np.radians(a))) for a in np.arange(geo['a0'], geo['a1'], 0.5)]
            z.line(pts, fill=(255, 140, 0), width=2)                                          # Bogen orange
            for a, l in erg['striche']['striche']:
                t = np.radians(a); haupt = a in erg['striche']['haupt']; L = r + 3 + l
                z.line([(cx + (r + 2) * np.cos(t), cy - (r + 2) * np.sin(t)), (cx + L * np.cos(t), cy - L * np.sin(t))], fill=(0, 200, 255) if not haupt else (255, 0, 255), width=3 if haupt else 1)
            for k in erg['ziffern']:
                z.rectangle([k['x0'] - 2, k['y0'] - 2, k['x1'] + 2, k['y1'] + 2], outline=(0, 255, 0) if k['hauptstrich'] else (255, 255, 0), width=2)
            z.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], outline=(255, 140, 0), width=2)
        im.save(aus, quality=92)
    return erg

if __name__ == '__main__':
    e = erkennen(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
    print(json.dumps(e.get('zusammenfassung') or {'fehler': 'kein Bogen'}, ensure_ascii=False))
