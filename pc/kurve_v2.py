#!/usr/bin/env python3
"""Kurve 0 → max → 0 mit der V2-Firmware: Modbus-Schreibsperre setzen, Prüfstrom je Schritt, Median-Lesung, Skala-/Zeigerzustand.
Aufruf: python3 kurve_v2.py [Schritt mA] [Max mA] [Haltezeit s]   (Standard 0.1 0.8 8)"""
import sys, subprocess, json, time
def cmd(u): return subprocess.run([sys.executable, 'esp_befehl.py', u], capture_output=True, text=True, timeout=60).stdout.strip()
schritt = float(sys.argv[1]) if len(sys.argv) > 1 else 0.1; mx = float(sys.argv[2]) if len(sys.argv) > 2 else 0.8; halt = float(sys.argv[3]) if len(sys.argv) > 3 else 8
auf = [round(i * schritt, 3) for i in range(int(round(mx / schritt)) + 1)]; folge = auf + auf[-2::-1]
cmd('/control?var=modbusschreiben&val=0'); time.sleep(1)
print(f"{'Soll':>6} {'Median':>7} {'Abw':>7} {'Einzel':>7} {'Sich':>5} {'Pkt':>4} Skala  Richtung")
abw = []
for k, w in enumerate(folge):
    cmd(f'/strom?wert={w}'); time.sleep(halt); j = json.loads(cmd('/status'))
    med = j.get('wert_median'); ist = j.get('wert'); s = j.get('sicherheit') or 0; sk = j['skala']
    ms = f'{med:7.3f}' if med is not None else 'unsich.'; ab = f'{med-w:+7.3f}' if med is not None else ''; es = f'{ist:7.3f}' if ist is not None else '   --- '
    print(f"{w:6.2f} {ms:>7} {ab:>7} {es:>7} {s:5.0f} {j['zeiger_punkte']:4d} {'ok' if sk['ok'] else 'FEHLT'} {sk['striche']:2d}/{sk['haupt']}  {'auf' if k < len(auf) else 'ab'}", flush=True)
    if med is not None: abw.append(abs(med - w))
cmd('/strom?wert=0'); cmd('/control?var=modbusschreiben&val=1')
if abw: print(f"max |Abw| {max(abw):.3f} mA, mittel {sum(abw)/len(abw):.3f} mA, n={len(abw)}/{len(folge)}")
