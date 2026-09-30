#!/usr/bin/env python3
"""Belichtungsversuch (16.09.): Raster aus Belichtungszeit × Verstärkung × LED am Gerät fahren, je Bild speichern und bewerten,
wie gut Striche und Ziffern lesbar sind. Bewertung je Bild (Skalenkreis offline per RANSAC gefunden):
  weiss = 95-%-Wert im Skalenband, schwarz = 5-%-Wert, kontrast = (weiss-schwarz)/weiss, saett = Anteil >= 250,
  striche = Zahl dunkler Täler auf dem Strichring, zkontrast = Kontrast im Ziffernband.
Aufruf: python3 belichtungstest.py NAME [--aec 200,400,700,1000,1200] [--gain 2,4,8,16] [--led 0,100,180,255]
Ergebnis: Bilder/belichtung_NAME/*.jpg + tabelle.md (nach Strichzahl und Kontrast sortiert). Regelung wird dafür abgeschaltet."""
import sys, time, json, subprocess, pathlib, shutil, argparse
import numpy as np
from PIL import Image
import zeiger_lesen as zl, skalenkreis as sk
HIER = pathlib.Path(__file__).resolve().parent; LIVE = HIER.parent / 'Bilder' / 'live' / 'aktuell.jpg'
ap = argparse.ArgumentParser(); ap.add_argument('name'); ap.add_argument('--aec', default='200,400,700,1000,1200'); ap.add_argument('--gain', default='2,4,8,16'); ap.add_argument('--led', default='0,100,180,255'); ap.add_argument('--warte', type=float, default=7)
a = ap.parse_args(); ziel = HIER.parent / 'Bilder' / f'belichtung_{a.name}'; ziel.mkdir(exist_ok=True)
def cmd(u): return subprocess.run([sys.executable, HIER / 'esp_befehl.py', u], capture_output=True, text=True, timeout=60).stdout.strip()

def bewerte(pfad):
    im = np.asarray(Image.open(pfad).convert('RGB')).astype(float); g = 0.299*im[...,0]+0.587*im[...,1]+0.114*im[...,2]; H, W = g.shape
    try:
        P, d, _ = sk.kandidaten(str(pfad)); cx, cy, r, n, a0, a1 = sk.ransac_kreis(P, H=H, W=W)
    except Exception: return None
    yy, xx = np.mgrid[0:H, 0:W]; rr = np.hypot(xx-cx, yy-cy); ang = np.degrees(np.arctan2(-(yy-cy), xx-cx))
    sekt = (ang > max(a0-2, 20)) & (ang < min(a1+2, 170))
    band = sekt & (rr > r-15) & (rr < r+60); ziff = sekt & (rr > r+25) & (rr < r+60)
    weiss = np.percentile(g[band], 95); schwarz = np.percentile(g[band], 5); saett = (g[band] >= 250).mean()
    zw, zs = np.percentile(g[ziff], 95), np.percentile(g[ziff], 5)
    # Strichzählung auf dem Strichring: Winkelprofil (Median über r±3), lokaler Median ±4°, Tal < Bezug − 0,4·max
    w = np.arange(a0, a1, 0.1); th = np.radians(w); prof = np.median(np.stack([g[np.clip((cy - rq*np.sin(th)).astype(int),0,H-1), np.clip((cx + rq*np.cos(th)).astype(int),0,W-1)] for rq in (r-3, r-1, r+1, r+3)]), axis=0)
    hb = 40; med = np.array([np.median(prof[max(0,i-hb):i+hb+1]) for i in range(len(prof))]); tal = med - prof; mx = tal.max()
    dunkel = tal > 0.4*mx; striche = int(np.sum(dunkel[1:] & ~dunkel[:-1]))
    return dict(cx=cx, cy=cy, r=r, inlier=n, weiss=weiss, schwarz=schwarz, kontrast=(weiss-schwarz)/max(weiss,1), saett=saett, striche=striche, zkontrast=(zw-zs)/max(zw,1), taltiefe=mx)

zeilen = []
print(cmd('/control?var=lichtregel&val=0')); cmd('/control?var=stream&val=1'); cmd('/control?var=overlay&val=0')
for led in [int(x) for x in a.led.split(',')]:
    cmd(f'/control?var=led&val={led},{led},{led}')
    for gain in [int(x) for x in a.gain.split(',')]:
        cmd(f'/control?var=gain&val={gain}')
        for aec in [int(x) for x in a.aec.split(',')]:
            cmd(f'/control?var=aec&val={aec}'); time.sleep(a.warte)
            pfad = ziel / f'aec{aec}_g{gain}_led{led}.jpg'; shutil.copy(LIVE, pfad)
            b = bewerte(pfad); z = dict(aec=aec, gain=gain, led=led, **(b or {}))
            zeilen.append(z); print(f"aec {aec:5d} gain {gain:2d} led {led:3d}: " + (f"weiss {b['weiss']:.0f} schwarz {b['schwarz']:.0f} kontrast {b['kontrast']:.2f} saett {100*b['saett']:.1f}% striche {b['striche']} zkontrast {b['zkontrast']:.2f} (r {b['r']:.0f}, {b['inlier']} Inl.)" if b else 'kein Skalenkreis'), flush=True)
cmd('/control?var=overlay&val=1'); cmd('/control?var=stream&val=0')
gut = [z for z in zeilen if z.get('striche') is not None]
gut.sort(key=lambda z: (-(z['striche']), -z['kontrast']))
with open(ziel / 'tabelle.md', 'w') as f:
    f.write(f'# Belichtungsversuch {a.name}\n\n| aec | gain | LED | Weiß | Schwarz | Kontrast | Sättigung % | Striche | Ziffernkontrast |\n|---|---|---|---|---|---|---|---|---|\n')
    for z in gut: f.write(f"| {z['aec']} | {z['gain']} | {z['led']} | {z['weiss']:.0f} | {z['schwarz']:.0f} | {z['kontrast']:.2f} | {100*z['saett']:.1f} | {z['striche']} | {z['zkontrast']:.2f} |\n")
    ohne = [z for z in zeilen if z.get('striche') is None]
    if ohne: f.write('\nOhne erkennbaren Skalenkreis: ' + ', '.join(f"aec {z['aec']}/g{z['gain']}/LED {z['led']}" for z in ohne) + '\n')
json.dump(zeilen, open(ziel / 'tabelle.json', 'w'), indent=1)
print('Tabelle:', ziel / 'tabelle.md'); print('Beste 5:'); [print(f"  aec {z['aec']} gain {z['gain']} LED {z['led']}: Striche {z['striche']}, Kontrast {z['kontrast']:.2f}, Weiß {z['weiss']:.0f}, Sättigung {100*z['saett']:.1f}%") for z in gut[:5]]
