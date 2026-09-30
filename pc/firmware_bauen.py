#!/usr/bin/env python3
"""Baugate für die Firmware: nur geprüfter Code wird zur Funk-Update-Datei.
1. Statische Prüfung aller snprintf/printf-Zeilen: Zahl der %-Platzhalter = Zahl der Argumente; Zeilenkommentare, die eine Anweisung verschluckt haben.
2. Übersetzen mit allen Warnungen; Formatwarnungen und einige weitere gelten als Fehler.
3. Erst bei Erfolg: .bin nach firmware/ kopieren (dort holt esp_zum_workspace.py die Datei fürs OTA).
Aufruf: python3 firmware_bauen.py   (im Ordner Arbeitsstand)"""
import re, subprocess, sys, pathlib, shutil
import os, tempfile
SKETCHNAME = sys.argv[1] if len(sys.argv) > 1 else 'ESP32S3_Zeiger_V2'           # Sketch-Ordner als Argument
HIER = pathlib.Path(__file__).resolve().parent
KANDIDATEN = [HIER / SKETCHNAME, HIER.parent / 'Firmware' / SKETCHNAME]         # Arbeitsstand/ oder Paketlayout Workspace/ + Firmware/
SKETCH = next((k for k in KANDIDATEN if k.is_dir()), KANDIDATEN[0])
INO = SKETCH / (SKETCHNAME + '.ino'); BUILD = pathlib.Path(tempfile.gettempdir()) / ('build_gate_' + SKETCHNAME)
ZIEL = (HIER / 'firmware' if (HIER / 'firmware').is_dir() or not (HIER.parent / 'Firmware' / 'bin').is_dir() else HIER.parent / 'Firmware' / 'bin') / (SKETCHNAME + '.bin')

def argzahl(text):
    """Argumente nach dem Formatstring zählen (Kommas auf Klammertiefe 0, Strings/Ternäre berücksichtigt)."""
    tief = 0; n = 0; instr = False; i = 0
    while i < len(text):
        c = text[i]
        if instr:
            if c == '\\': i += 1
            elif c == '"': instr = False
        elif c == '"': instr = True
        elif c in '([{': tief += 1
        elif c in ')]}':
            if tief == 0: break
            tief -= 1
        elif c == ',' and tief == 0: n += 1
        i += 1
    return n + 1

def kommentarpruefung(quelle):
    """Zeilenkommentare, die Code verschluckt haben: nach '//' folgt noch etwas, das wie eine Anweisung aussieht
    (z. B. 'vsIndex = -1;' oder 'static char x[3] = "-";'). Entstanden zweimal durch Ersetzungen, deren Text mit einem
    Kommentar endete, während die Originalzeile weiterging (14./15.09.). Kommentare mit Prosa (Umlaute, 'z. B.', Doppelpunkt
    vor dem Semikolon) werden nicht gemeldet."""
    treffer = []
    muster = re.compile(r'(^|[;{}\s])(static\s+)?[A-Za-z_]\w*(\[[^\]]*\])?\s*=\s*[^=;]{1,40};\s*$')
    for nr, zeile in enumerate(quelle.splitlines(), 1):
        if '//' not in zeile or zeile.lstrip().startswith('//'): continue
        vor, _, kom = zeile.partition('//')
        if vor.count('"') % 2: continue                                     # '//' innerhalb eines Strings
        kom = kom.strip()
        if muster.search(kom) and not re.search(r'[äöüÄÖÜ:–]|\bz\. B\.', kom):
            treffer.append(f'Zeile {nr}: Kommentar endet mit einer Anweisung: //{kom[-90:]}')
        elif re.search(r'\)\s*;|;\s*\}|\}\s*$|=\s*(min|max)\(', kom):        # verschluckter Codeblock (16.09.: Ersetzung mit Endkommentar zog Schleifenrumpf in den Kommentar)
            treffer.append(f'Zeile {nr}: Kommentar enthält Code: //{kom[-90:]}')
    return treffer

def formatpruefung(quelle):
    """Alle snprintf/printf-Aufrufe der Datei (auch mehrzeilig): Platzhalter im Formatliteral gegen Argumentzahl."""
    fehler = []
    for m in re.finditer(r'\b(snprintf|printf)\s*\(', quelle):
        nr = quelle.count('\n', 0, m.start()) + 1
        rest = quelle[m.end():]
        teile = []; tief = 0; buf = ''; instr = False; j = 0
        while j < len(rest):
            c = rest[j]
            if instr:
                buf += c
                if c == '\\': buf += rest[j + 1]; j += 1
                elif c == '"': instr = False
            elif c == '"': instr = True; buf += c
            elif c in '([{': tief += 1; buf += c
            elif c in ')]}':
                if tief == 0: teile.append(buf); break
                tief -= 1; buf += c
            elif c == ',' and tief == 0: teile.append(buf); buf = ''
            else: buf += c
            j += 1
        fmt_i = 2 if m.group(1) == 'snprintf' else 0
        if len(teile) <= fmt_i: continue
        fmt = teile[fmt_i].strip()
        if not fmt.startswith('"'): continue                          # kein Literal: nicht prüfbar
        platz = len(re.findall(r'%(?:[-+ #0]*\d*(?:\.\d+)?(?:hh|h|l|ll|z)?[diuoxXfFeEgGcsp])', fmt))
        args = len(teile) - fmt_i - 1
        if platz != args: fehler.append(f'Zeile {nr}: {platz} Platzhalter, aber {args} Argumente')
    return fehler

if ZIEL.exists(): ZIEL.unlink(); print('alte Freigabe gelöscht:', ZIEL.name)   # ein fehlgeschlagener Bau darf nie das alte Binary zum OTA lassen (16.09.)
quelle = '\n'.join(q.read_text(encoding='utf-8') for q in sorted(list(SKETCH.glob('*.ino')) + list(SKETCH.glob('*.h')) + list(SKETCH.glob('*.cpp'))))
f = formatpruefung(quelle)
if f:
    print('FORMATPRÜFUNG FEHLGESCHLAGEN:'); print('\n'.join('  ' + x for x in f)); sys.exit(1)
print('Formatprüfung: alle snprintf/printf-Zeilen stimmig')
k = kommentarpruefung(quelle)
if k:
    print('KOMMENTARPRÜFUNG FEHLGESCHLAGEN (Ersetzung hat Code in einen Zeilenkommentar gezogen?):'); print('\n'.join('  ' + x for x in k)); sys.exit(1)
print('Kommentarprüfung: keine verschluckten Anweisungen')
cmd = ['arduino-cli', 'compile', '--warnings', 'all', '--build-property', 'compiler.warning_flags.all=-Wall -Wextra -Werror=format -Werror=format-extra-args -Werror=return-type -Werror=uninitialized -Werror=sequence-point',
       '--build-path', str(BUILD), str(SKETCH)]
r = subprocess.run(cmd, capture_output=True, text=True)
warn = [l for l in r.stderr.split('\n') if 'warning:' in l and str(SKETCH) in l]
err = [l for l in r.stderr.split('\n') if 'error' in l.lower()]
for l in warn: print('Warnung:', l.split('ESP32S3_Zeiger_Live/')[-1][:160])
if r.returncode != 0:
    print('ÜBERSETZUNG FEHLGESCHLAGEN:'); print('\n'.join('  ' + l[:200] for l in err[:10])); sys.exit(1)
groesse = [l for l in r.stdout.split('\n') if 'Sketch uses' in l]
print(groesse[0] if groesse else 'übersetzt')
bin_ = BUILD / (SKETCHNAME + '.ino.bin'); ZIEL.parent.mkdir(exist_ok=True); shutil.copy(bin_, ZIEL)
print('Firmware freigegeben:', ZIEL, bin_.stat().st_size, 'Byte')
