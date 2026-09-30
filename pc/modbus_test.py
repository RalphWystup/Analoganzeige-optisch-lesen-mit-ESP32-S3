#!/usr/bin/env python3
"""Modbus-TCP-Prüfung vom PC aus (Heimnetz), ohne Bibliothek – eigenstaendig, ohne weitere Dateien.
  python modbus_test.py                 liest Register 0–14 und deutet sie
  python modbus_test.py strom 0.4       setzt den Prüfstrom über Register 8–9 (FC16)
  python modbus_test.py lesen 0 2       liest nur den Messwert
Trendows: Gerät „Modbus TCP", IP des ESP, Port 502, Adresse 1, Holding-Register (FC3), Float 2 Register High-Word zuerst.
"""
import sys, time

def modbus_anfrage(spec, host=None, port=502, adresse=1):
    """Modbus TCP am ESP: spec = 'lesen ADR ANZ' (FC3), 'strom WERT' (FC16 auf Register 8-9, Float High-Word zuerst) oder 'ua N' (FC6 auf Register 15, µA).
    Antwort als Text mit Registern und den daraus gelesenen Größen. Eigener Sockel, keine Bibliothek nötig."""
    import socket, struct
    host = host or input('IP des Geraetes (aus der seriellen Ausgabe): ').strip(); teile = spec.split()
    if teile[0] == 'lesen':
        adr, anz = int(teile[1]), int(teile[2]); pdu = struct.pack('>BHH', 3, adr, anz)
    elif teile[0] == 'strom':
        pdu = struct.pack('>BHHB', 16, 8, 2, 4) + struct.pack('>f', float(teile[1]))
    elif teile[0] == 'ua':                     # ganzzahlig in Mikroampere auf Register 15 (FC6)
        pdu = struct.pack('>BHH', 6, 15, int(teile[1]))
    elif teile[0] == 'ua16':                   # dasselbe als FC16 mit einem Register (Telegramm wie Trendows WO (16))
        pdu = struct.pack('>BHHBH', 16, 15, 1, 2, int(teile[1]))
    else: return 'FEHLER: spec muss "lesen ADR ANZ" oder "strom WERT" sein'
    frage = struct.pack('>HHHB', 0x4711, 0, len(pdu) + 1, adresse) + pdu
    t0 = time.time()
    with socket.create_connection((host, port), timeout=3) as sk:
        sk.sendall(frage); kopf = sk.recv(7)
        if len(kopf) < 7: return 'FEHLER: kein MBAP-Kopf'
        tid, pid, laenge, uid = struct.unpack('>HHHB', kopf); rest = b''
        while len(rest) < laenge - 1: rest += sk.recv(laenge - 1 - len(rest))
    dauer = (time.time() - t0) * 1000
    fc = rest[0]
    if fc & 0x80: return f'AUSNAHME FC{fc & 0x7F} Code {rest[1]} ({dauer:.0f} ms)'
    if fc == 6: return f'FC6 ok: Register {struct.unpack(">H", rest[1:3])[0]} = {struct.unpack(">H", rest[3:5])[0]} ({dauer:.0f} ms)'
    if fc == 16: return f'FC16 ok: Register {struct.unpack(">H", rest[1:3])[0]}, {struct.unpack(">H", rest[3:5])[0]} geschrieben ({dauer:.0f} ms)'
    n = rest[1] // 2; reg = struct.unpack(f'>{n}H', rest[2:2 + 2 * n]); adr = int(teile[1])
    def fl(i):
        j = i - adr
        return struct.unpack('>f', struct.pack('>HH', reg[j], reg[j + 1]))[0] if 0 <= j and j + 1 < n else None
    def r(i): return reg[i - adr] if 0 <= i - adr < n else None
    text = f'FC3 TID {tid:04X} {n} Register ab {adr} ({dauer:.0f} ms): ' + ' '.join(str(x) for x in reg)
    deut = []
    if fl(0) is not None: deut.append(f'Messwert {fl(0):.3f}')
    if fl(2) is not None: deut.append(f'Winkel {fl(2):.2f}')
    for i, name in ((4, 'gueltig'), (5, 'Sicherheit'), (6, 'Guete%'), (7, 'Bild'), (10, 'Dauer_ms'), (11, 'Ist'), (12, 'LED'), (13, 'kalibriert'), (14, 'Hintergrund'), (15, 'Pruefstrom_uA')):
        if r(i) is not None: deut.append(f'{name} {r(i)}')
    if fl(8) is not None: deut.append(f'Pruefstrom {fl(8):.3f}')
    return text + ' | ' + ', '.join(deut)


spec = ' '.join(sys.argv[1:]) or 'lesen 0 15'
print(modbus_anfrage(spec))
