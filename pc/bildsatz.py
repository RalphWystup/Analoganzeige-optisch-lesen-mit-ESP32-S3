#!/usr/bin/env python3
"""Bildsatz bei bekannten Prüfströmen aufnehmen: Rohbild 640×480 ohne Einzeichnungen + Status je Strom."""
import sys, subprocess, json, time, pathlib, shutil
def cmd(u): return subprocess.run([sys.executable, 'esp_befehl.py', u], capture_output=True, text=True, timeout=60).stdout.strip()
ziel = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path('../Bilder/44C2_satz'); ziel.mkdir(exist_ok=True)
stroeme = [float(x) for x in sys.argv[2].split(',')] if len(sys.argv) > 2 else [i / 10 for i in range(10)]
cmd('/control?var=overlay&val=0'); cmd('/control?var=stream&val=1')
for w in stroeme:
    cmd(f'/strom?wert={w}'); time.sleep(8)
    j = json.loads(cmd('/status')); name = f'{w:.2f}'.replace('.', 'p')
    shutil.copy('../Bilder/live/aktuell.jpg', ziel / f'{name}.jpg'); json.dump(j, open(ziel / f'{name}.json', 'w'), indent=1)
    print(f"{w:5.2f} mA: Bild gespeichert, ESP liest {j.get('wert')} (Sicherheit {j.get('sicherheit')})")
cmd('/strom?wert=0'); cmd('/control?var=overlay&val=1'); cmd('/control?var=stream&val=0')
