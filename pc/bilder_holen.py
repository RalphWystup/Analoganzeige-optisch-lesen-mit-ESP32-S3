#!/usr/bin/env python3
"""Standbilder vom Freenove ESP32-S3 holen und im Bildordner ablegen.

Ohne Argumente (z. B. Start in Thonny mit F5): fragt nach dem Bildnamen, Enter holt ein Bild,
leere Eingabe oder q beendet. Adresse ist HOST_STANDARD (bei Bedarf unten ändern).

Kommandozeile:  python3 bilder_holen.py [IP oder Hostname] [--anzahl N] [--intervall SEK]
                                        [--groesse svga|hd|uxga|qxga] [--name TEXT]
Beispiel:       python3 bilder_holen.py <IP-des-Boards> --anzahl 5 --intervall 3 --name 0mA
Dateien:        ../Bilder/JJJJ-MM-TT_HHMMSS_<name>_<groesse>.jpg  (einziger Ablageort für Bilder)
"""
import sys, time, argparse, urllib.request, pathlib, datetime, json

HOST_STANDARD = '<IP-des-Boards>'      # Adresse aus der seriellen Ausgabe; alternativ 'analogcam.local'

p = argparse.ArgumentParser()
p.add_argument('host', nargs='?', default=HOST_STANDARD)
p.add_argument('--anzahl', type=int, default=1); p.add_argument('--intervall', type=float, default=2.0)
p.add_argument('--groesse', default='uxga', choices=['svga', 'hd', 'uxga', 'qxga']); p.add_argument('--name', default=None)
a = p.parse_args()
ziel = pathlib.Path(__file__).resolve().parent.parent / 'Bilder'; ziel.mkdir(exist_ok=True)
print(f'Kamera: http://{a.host}/   Bilder nach: {ziel}')

try:
    status = json.loads(urllib.request.urlopen(f'http://{a.host}/status', timeout=5).read().decode())
    print('Status:', status)
except Exception as e:
    print('Status nicht lesbar:', e); print('Läuft der ESP32-S3? Stimmt die Adresse (serieller Monitor, Zeile [BEREIT])?'); sys.exit(1)

def bild_holen(name, groesse):
    t0 = time.time()
    with urllib.request.urlopen(f'http://{a.host}/capture?size={groesse}', timeout=30) as r:
        daten = r.read(); g = r.headers.get('X-Image-Size', groesse)
    if daten[:2] != b'\xff\xd8':
        print('Kein JPEG erhalten'); return False
    datei = ziel / f"{datetime.datetime.now():%Y-%m-%d_%H%M%S}_{name}_{g}.jpg"
    datei.write_bytes(daten)
    print(f'  gespeichert: {datei.name} ({len(daten)/1024:.0f} kB, {g}, {time.time()-t0:.1f} s)')
    return True

if a.name is None and a.anzahl == 1:
    # Interaktiv: je Eingabe ein Bild. Als Name den Stromwert eingeben, z. B. 0A, 1A5, 2A.
    print('Bildname eingeben (z. B. Stromwert wie 1A5), Enter holt ein Bild; leer oder q beendet.')
    while True:
        try: name = input('Name> ').strip()
        except EOFError: break
        if name in ('', 'q', 'Q'): break
        bild_holen(name.replace(' ', '_'), a.groesse)
else:
    name = a.name or 'test'
    for i in range(a.anzahl):
        print(f'{i+1}/{a.anzahl}')
        if not bild_holen(name, a.groesse): sys.exit(1)
        if i + 1 < a.anzahl: time.sleep(a.intervall)
