#!/usr/bin/env python3
"""Kurzer Blick auf die Live-Reihe: letzte Verlaufszeilen und Pfad des aktuellen Bildes."""
import json, pathlib, sys
LIVE = pathlib.Path(__file__).resolve().parent.parent / 'Bilder' / 'live'
n = int(sys.argv[1]) if len(sys.argv) > 1 else 12
zeilen = (LIVE / 'verlauf.jsonl').read_text().splitlines()[-n:] if (LIVE / 'verlauf.jsonl').exists() else []
print(f"{'Zeit':8} {'Nr':>6} {'ms':>5} {'Linie°':>7} {'Pkt':>4} {'Wert':>8} {'Winkel':>7} {'Sich':>5}  Lage")
for z in zeilen:
    j = json.loads(z); f = lambda v, fmt: (fmt % v) if v is not None else '–'
    print(f"{j['zeit']:8} {j['name'].replace('live_','').split('.')[0]:>6} {f(j['ms'],'%5d'):>5} {f(j['winkel180'],'%7.2f'):>7} {f(j['punkte'],'%4d'):>4} {f(j['wert'],'%8.2f'):>8} {f(j['winkel'],'%7.2f'):>7} {f(j['sicherheit'],'%5.0f'):>5}  {j['lage']}")
print('aktuelles Bild:', LIVE / 'aktuell.jpg', '| Dateien:', len(list(LIVE.glob('*.jpg'))))
