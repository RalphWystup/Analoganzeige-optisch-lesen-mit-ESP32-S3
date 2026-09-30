#!/usr/bin/env python3
"""Zeigerschritt V2 (offline, 16.09.): Zeiger relativ zur erkannten Skala (skala_v2) messen.
Je Bild: Skala erkennen (Bogen, Hauptstriche ↔ Werte nach Reihenfolge), Rotbild mit Weißabgleich am Zifferblatt, dünne dunkle
Linien innerhalb des Bogens, RANSAC-Gerade = Zeiger. Der Drehpunkt ist NICHT der Bogenmittelpunkt (Parallaxe: Zeiger liegt
über dem Zifferblatt): er wird als Schnittpunkt aller Zeigergeraden des Bildsatzes bestimmt und als Versatz zum Bogen-
mittelpunkt gemerkt. Wert = Zeigerwinkel vom Drehpunkt aus, stückweise linear zwischen den Hauptstrichen (deren Winkel
ebenfalls vom Drehpunkt aus gerechnet).
Aufruf: python3 zeiger_v2.py ORDNER [BEREICH=1.0]   (Bilder NAME.jpg mit Strom im Namen wie 0p40 = 0,40 mA)"""
import sys, glob, json, pathlib, numpy as np
from PIL import Image, ImageDraw
import zeiger_lesen as zl, skala_v2 as sv
rng = np.random.default_rng(2)

def rotbild(im, geo):
    a = np.asarray(im.convert('RGB')).astype(np.float32); H, W, _ = a.shape; cx, cy, r = geo['cx'], geo['cy'], geo['r']
    yy, xx = np.mgrid[0:H, 0:W]; rr = np.hypot(xx - cx, yy - cy); ang = np.degrees(np.arctan2(-(yy - cy), xx - cx))
    ring = (rr > 0.55 * r) & (rr < 0.95 * r) & (ang > geo['a0']) & (ang < geo['a1'])
    wb = np.array([np.median(a[..., c][ring]) for c in range(3)]); w = np.clip(a * (wb.mean() / wb), 0, 255)
    rot = 255 - 255 * np.clip(w[..., 0] - np.maximum(w[..., 1], w[..., 2]), 0, None) / np.maximum(w[..., 0], 1)
    return rot, ring, rr, ang

def zeiger_gerade(rot, geo, rr, ang):
    d = zl.duenne_dunkle_linien(rot, 9); H, W = d.shape; cx, cy, r = geo['cx'], geo['cy'], geo['r']
    zone = (rr > 0.3 * r) & (rr < r - 4) & (ang > geo['a0'] - 6) & (ang < geo['a1'] + 6)
    dz = np.where(zone, d, 0); sw = max(12.0, np.quantile(dz[zone], 0.985)); ys, xs = np.nonzero(dz >= sw)
    if len(xs) < 30: return None
    P = np.stack([xs, ys], 1).astype(float); best, bn = None, 0
    for _ in range(600):
        i, j = rng.choice(len(P), 2, replace=False); p, q = P[i], P[j]; v = q - p; L = np.hypot(*v)
        if L < 20: continue
        v /= L; nrm = np.array([-v[1], v[0]]); dist = np.abs((P - p) @ nrm); inl = dist < 2.0; m = int(inl.sum())
        if m > bn: bn, best = m, (p, v, inl)
    p, v, inl = best; Q = P[inl]; m = Q.mean(0); u, s_, vt = np.linalg.svd(Q - m); v = vt[0] / np.linalg.norm(vt[0])
    if (m - np.array([cx, cy])) @ v < 0: v = -v                                  # Richtung vom Mittelpunkt nach außen
    return dict(px=float(m[0]), py=float(m[1]), dx=float(v[0]), dy=float(v[1]), n=int(inl.sum()))

def winkel(dx, dy): return float(np.degrees(np.arctan2(-dy, dx)) % 360)

def auswerten(ordner, bereich=1.0, aus=None):
    bilder = sorted(glob.glob(str(pathlib.Path(ordner) / '*.jpg'))); saetze = []
    for b in bilder:
        name = pathlib.Path(b).stem
        try: strom = float(name.replace('p', '.'))
        except ValueError: continue
        im = Image.open(b); geo_all = sv.erkennen(b); geo = geo_all.get('bogen'); st = geo_all.get('striche') or {}
        if not geo or len(st.get('haupt', [])) < 4: print(f'{name}: Skala nicht vollständig'); continue
        rot, ring, rr, ang = rotbild(im, geo); L = zeiger_gerade(rot, geo, rr, ang)
        if not L: print(f'{name}: kein Zeiger'); continue
        saetze.append(dict(name=name, strom=strom, geo=geo, haupt=sorted(st['haupt'], reverse=True), linie=L, bild=b, striche=len(st['striche']), ziffern=geo_all.get('ziffern') or []))
    if len(saetze) < 3: print('zu wenige Bilder'); return
    # Drehpunkt = Schnittpunkt aller Geraden (kleinste Quadrate)
    A = np.zeros((2, 2)); bvec = np.zeros(2)
    for s_ in saetze:
        L = s_['linie']; d = np.array([L['dx'], L['dy']]); M = np.eye(2) - np.outer(d, d); A += M; bvec += M @ np.array([L['px'], L['py']])
    P = np.linalg.solve(A, bvec); C = np.mean([[s_['geo']['cx'], s_['geo']['cy']] for s_ in saetze], axis=0); R = np.mean([s_['geo']['r'] for s_ in saetze])
    print(f'Bogenmittelpunkt ({C[0]:.1f}, {C[1]:.1f}) r {R:.1f} | Drehpunkt aus {len(saetze)} Geraden ({P[0]:.1f}, {P[1]:.1f}) | Versatz ({P[0]-C[0]:+.1f}, {P[1]-C[1]:+.1f}) px = {100*np.hypot(*(P-C))/R:.1f} % von r')
    print(f"{'Strom':>6} {'Wert(P)':>8} {'Abw':>7} {'Wert(C)':>8} {'Abw':>7} {'Wert(S)':>8} {'Abw':>7}  Striche  Linienpunkte   (P Drehpunkt, C Bogenmitte, S Schnittpunkt Gerade/Bogen)")
    fehler_P, fehler_C, fehler_S = [], [], []
    for s_ in saetze:
        g = s_['geo']; L = s_['linie']; haupt = s_['haupt']; n = len(haupt); werte = [bereich * k / (n - 1) for k in range(n)]
        def wert_aus(punkt):
            # Hauptstrichwinkel von 'punkt' aus: Strichfuß auf dem Bogen
            tw = []
            for a in haupt:
                t = np.radians(a); x = g['cx'] + g['r'] * np.cos(t); y = g['cy'] - g['r'] * np.sin(t); tw.append(winkel(x - punkt[0], y - punkt[1]))
            # Zeigerwinkel von 'punkt' aus: Richtung der Geraden
            zw = winkel(L['dx'], L['dy'])
            tw = np.array(tw); w = np.array(werte)                            # tw fällt mit steigendem Wert
            return float(np.interp(-zw, -tw, w))                              # monoton machen
        vP, vC = wert_aus(P), wert_aus(C); fehler_P.append(vP - s_['strom']); fehler_C.append(vC - s_['strom'])
        # Variante S (Nutzervorschlag 16.09.): Schnittpunkt der Zeigergeraden mit dem Skalenbogen, Ablesung dort zwischen den
        # Hauptstrichen (Winkel vom Bogenmittelpunkt aus, wie die Striche selbst) – ohne Drehpunkt
        ox, oy = L['px'] - g['cx'], L['py'] - g['cy']; d = np.array([L['dx'], L['dy']]); bq = 2 * (ox * d[0] + oy * d[1]); cq = ox * ox + oy * oy - g['r'] ** 2
        disc = bq * bq - 4 * cq; vS = float('nan')
        if disc >= 0:
            t1 = (-bq + np.sqrt(disc)) / 2; t2 = (-bq - np.sqrt(disc)) / 2; t = max(t1, t2)                    # Schnittpunkt in Zeigerrichtung (nach außen)
            sx, sy = L['px'] + t * d[0], L['py'] + t * d[1]; ws = winkel(sx - g['cx'], sy - g['cy'])
            tw_c = np.array(haupt); vS = float(np.interp(-ws, -tw_c, np.array(werte)))
        fehler_S.append(vS - s_['strom']); s_['vS'] = vS
        print(f"{s_['strom']:6.2f} {vP:8.3f} {vP-s_['strom']:+7.3f} {vC:8.3f} {vC-s_['strom']:+7.3f} {vS:8.3f} {vS-s_['strom']:+7.3f}  {s_['striche']:7d}  {L['n']:12d}")
        if aus:
            im = Image.open(s_['bild']).convert('RGB'); z = ImageDraw.Draw(im)
            pts = [(g['cx'] + g['r'] * np.cos(np.radians(a)), g['cy'] - g['r'] * np.sin(np.radians(a))) for a in np.arange(g['a0'], g['a1'], 0.5)]
            z.line(pts, fill=(255, 140, 0), width=2)
            for a, wv in zip(haupt, werte):
                t = np.radians(a); z.line([(g['cx'] + (g['r'] + 2) * np.cos(t), g['cy'] - (g['r'] + 2) * np.sin(t)), (g['cx'] + (g['r'] + 22) * np.cos(t), g['cy'] - (g['r'] + 22) * np.sin(t))], fill=(255, 0, 255), width=3)
                tx, ty = g['cx'] + (g['r'] - 16) * np.cos(t), g['cy'] - (g['r'] - 16) * np.sin(t)          # erkannter Wert unter dem Strich (innen)
                txt = f'{wv:g}'; z.rectangle([tx - 11, ty - 7, tx + 11, ty + 7], fill=(255, 255, 255)); z.text((tx - 9, ty - 6), txt, fill=(200, 0, 200))
            for k in (s_.get('ziffern') or []): z.rectangle([k['x0'] - 2, k['y0'] - 2, k['x1'] + 2, k['y1'] + 2], outline=(0, 200, 0), width=2)   # erkannte Ziffernkästen
            t = np.radians(winkel(L['dx'], L['dy'])); z.line([(P[0], P[1]), (P[0] + (g['r'] + 10) * np.cos(t), P[1] - (g['r'] + 10) * np.sin(t))], fill=(0, 90, 255), width=2)
            z.ellipse([P[0] - 5, P[1] - 5, P[0] + 5, P[1] + 5], outline=(0, 90, 255), width=2); z.ellipse([C[0] - 4, C[1] - 4, C[0] + 4, C[1] + 4], outline=(255, 140, 0), width=2)
            z.text((10, 10), f"{s_['strom']:.2f} mA -> {vP:.3f} mA", fill=(0, 90, 255))
            pathlib.Path(aus).mkdir(exist_ok=True); im.save(pathlib.Path(aus) / f"{s_['name']}_zeiger.jpg", quality=90)
    print(f'Abweichung: P (Drehpunkt) max {max(map(abs, fehler_P)):.3f} mittel {np.mean(np.abs(fehler_P)):.3f} | C (Bogenmitte) max {max(map(abs, fehler_C)):.3f} | S (Schnittpunkt Gerade/Bogen, ohne Drehpunkt) max {max(map(abs, fehler_S)):.3f} mittel {np.mean(np.abs(fehler_S)):.3f} mA')

if __name__ == '__main__':
    auswerten(sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 1.0, sys.argv[3] if len(sys.argv) > 3 else None)
