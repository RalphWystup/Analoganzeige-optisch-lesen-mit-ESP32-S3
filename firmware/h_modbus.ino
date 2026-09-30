// ---------------------------------------------------------------------------------------------------------------------
//  h_modbus: Modbus TCP (aus Version 1, Register an V2 angepasst)
static const uint16_t MODBUS_PORT = 502; static const int MODBUS_MAX_CLIENTS = 2; static const uint16_t MODBUS_REGISTER = 16;
static WiFiServer modbusServer(MODBUS_PORT); static WiFiClient modbusClients[MODBUS_MAX_CLIENTS]; static uint32_t modbusZuletzt[MODBUS_MAX_CLIENTS] = {0};
static uint32_t modbusKopf = 0;
static void floatInRegister(float f, uint16_t* r) { uint32_t u; memcpy(&u, &f, 4); r[0] = (uint16_t)(u >> 16); r[1] = (uint16_t)(u & 0xFFFF); }
static float registerInFloat(const uint16_t* r) { uint32_t u = ((uint32_t)r[0] << 16) | r[1]; float f; memcpy(&f, &u, 4); return f; }
static void modbusRegisterFuellen(uint16_t* r) {
  // V2-Belegung (mit V1 verträglich): 0–1 Messwert (letzter gültiger Median), 2–3 Zeigerwinkel, 4 gültig (Skala UND Zeiger),
  // 5 Sicherheit (Punkte der Zeigergeraden), 6 Skalengüte ×100 (statt Verfolgungsgüte), 7 Bildzähler, 8–9 Prüfstrom,
  // 10 Bilddauer ms, 11 Weiß des Zifferblatts (p95), 12 LED, 13 kalibriert, 14 Skala in diesem Bild gültig, 15 Prüfstrom µA
  floatInRegister(isnan(wertMedian) ? 0.0f : wertMedian, r + 0);
  floatInRegister(ZEIGER.linie.ok ? ZEIGER.winkel : 0.0f, r + 2);
  r[4] = ZEIGER.ok ? 1 : 0; r[5] = (uint16_t)constrain((int)(ZEIGER.sicherheit + 0.5f), 0, 65535);
  r[6] = (uint16_t)constrain((int)(SKALA.guete * 100.0f + 0.5f), 0, 100); r[7] = (uint16_t)(bildzaehler & 0xFFFF);
  floatInRegister(stromSoll, r + 8); r[10] = (uint16_t)min(dauerMs, (uint32_t)65535);
  r[11] = (uint16_t)constrain((int)(weiss95 + 0.5f), 0, 255); r[12] = (uint16_t)constrain((int)ledR, 0, 255);
  r[13] = KAL.ok ? 1 : 0; r[14] = SKALA.ok ? 1 : 0; r[15] = (uint16_t)constrain((int)(stromSoll * 1000.0f + 0.5f), 0, 65535);
}
// Eine Anfrage bedienen: Kopf (7 Byte) + PDU liegen vollständig vor. Antwort in aus[], Rückgabe = Länge.
static int modbusAntwort(const uint8_t* an, int nan, uint8_t* aus) {
  uint16_t tid = (an[0] << 8) | an[1], pid = (an[2] << 8) | an[3]; uint8_t uid = an[6], fc = an[7];
  aus[0] = tid >> 8; aus[1] = tid & 0xFF; aus[2] = pid >> 8; aus[3] = pid & 0xFF; aus[6] = uid;
  auto ausnahme = [&](uint8_t code) {
    aus[4] = 0; aus[5] = 3; aus[7] = fc | 0x80; aus[8] = code; modbusFehler++;
    snprintf(modbusLetzterFehler, sizeof(modbusLetzterFehler), "FC%d Code %d: %u Byte, adr %u anz %u", fc, code, nan, nan >= 10 ? (unsigned)((an[8] << 8) | an[9]) : 0u, nan >= 12 ? (unsigned)((an[10] << 8) | an[11]) : 0u);
    return 9; };
  uint16_t reg[MODBUS_REGISTER]; modbusRegisterFuellen(reg);
  if (fc == 3 || fc == 4) {
    if (nan < 12) return ausnahme(3);
    uint16_t adr = (an[8] << 8) | an[9], anz = (an[10] << 8) | an[11];
    if (anz < 1 || anz > 125 || adr + anz > MODBUS_REGISTER) return ausnahme(2);
    aus[7] = fc; aus[8] = anz * 2;
    for (int i = 0; i < anz; i++) { aus[9 + 2 * i] = reg[adr + i] >> 8; aus[10 + 2 * i] = reg[adr + i] & 0xFF; }
    uint16_t len = 3 + anz * 2; aus[4] = len >> 8; aus[5] = len & 0xFF;
    snprintf(modbusLetzter, sizeof(modbusLetzter), "FC%d Reg %u..%u", fc, adr, adr + anz - 1);
    return 6 + len;
  }
  auto sollUebernehmen = [&](float soll, const char* quelle) {   // unzulässige Werte annehmen, aber verwerfen (s. u.)
    bool gesperrt = !modbusSchreiben || kalAnfrage;   // Kalibrierung/Abgleich: Prüfstrom bleibt beim Gerät
    if (gesperrt) { modbusVerworfen++; snprintf(modbusLetzterFehler, sizeof(modbusLetzterFehler), "%s Wert %g verworfen (Schreiben gesperrt)", quelle, soll); return; }
    if (isnan(soll) || soll < 0 || soll > stromImax) { modbusVerworfen++; snprintf(modbusLetzterFehler, sizeof(modbusLetzterFehler), "%s Wert %g verworfen (zulaessig 0..%.3f)", quelle, soll, stromImax); }
    else { stromSetzen(soll); snprintf(modbusLetzter, sizeof(modbusLetzter), "%s Strom %.3f", quelle, soll); }
  };
  if (fc == 6) {                                    // ein Register schreiben: nur Register 15 (Prüfstrom in µA, ganzzahlig)
    if (nan < 12) return ausnahme(3);
    uint16_t adr = (an[8] << 8) | an[9], wert = (an[10] << 8) | an[11];
    if (adr != 15) return ausnahme(2);
    sollUebernehmen(wert / 1000.0f, "FC6");
    aus[4] = 0; aus[5] = 6; aus[7] = 6; aus[8] = an[8]; aus[9] = an[9]; aus[10] = an[10]; aus[11] = an[11];
    return 12;
  }
  if (fc == 16 && nan >= 15 && ((an[8] << 8) | an[9]) == 15 && ((an[10] << 8) | an[11]) == 1 && an[12] == 2) {   // FC16 auf Register 15: 7 + 1 + 2 + 2 + 1 + 2 = 15 Byte (Trendows WO (16))
    sollUebernehmen((float)((an[13] << 8) | an[14]) / 1000.0f, "FC16/15");
    aus[4] = 0; aus[5] = 6; aus[7] = 16; aus[8] = an[8]; aus[9] = an[9]; aus[10] = an[10]; aus[11] = an[11];
    return 12;
  }
  if (fc == 16) {                                   // Register 8–9 (Prüfstrom als Float)
    if (nan < 13) return ausnahme(3);
    uint16_t adr = (an[8] << 8) | an[9], anz = (an[10] << 8) | an[11]; uint8_t nb = an[12];
    if (adr != 8 || anz != 2 || nb != 4 || nan < 17) return ausnahme(2);
    uint16_t w[2] = { (uint16_t)((an[13] << 8) | an[14]), (uint16_t)((an[15] << 8) | an[16]) }; float soll = registerInFloat(w);
    // Unzulässige Sollwerte (NaN, negativ, über imax) werden angenommen, aber verworfen: Trendows schreibt Ausgänge in
    // jedem Zyklus; eine Fehlerantwort je Zyklus ließe die Verbindung dort als gestört erscheinen (14.09.: Wert 102 mA).
    sollUebernehmen(soll, "FC16");
    aus[4] = 0; aus[5] = 6; aus[7] = 16; aus[8] = an[8]; aus[9] = an[9]; aus[10] = an[10]; aus[11] = an[11];
    return 12;
  }
  return ausnahme(1);
}
static void modbusTask(void*) {
  uint8_t an[64], aus[6 + 3 + 2 * MODBUS_REGISTER + 8];
  for (;;) {
    WiFiClient neu = modbusServer.accept();
    if (neu) { int frei = -1; for (int i = 0; i < MODBUS_MAX_CLIENTS; i++) if (!modbusClients[i] || !modbusClients[i].connected()) { frei = i; break; }
      if (frei < 0) { neu.stop(); modbusFehler++; modbusVoll++; } else { neu.setNoDelay(true); modbusClients[frei] = neu; modbusZuletzt[frei] = millis(); } }
    for (int i = 0; i < MODBUS_MAX_CLIENTS; i++) {
      WiFiClient& c = modbusClients[i]; if (!c) continue;
      if (!c.connected()) { c.stop(); continue; }                                    // Gegenseite hat geschlossen: Sockel sofort freigeben (CLOSE_WAIT)
      int verf = c.available();
      if (verf < 7) { if (millis() - modbusZuletzt[i] > 60000UL) { c.stop(); modbusKopf++; snprintf(modbusLetzterFehler, sizeof(modbusLetzterFehler), "Verbindung %d ohne Verkehr getrennt (60 s)", i); } continue; }   // hängende Verbindungen (kein FIN) freigeben, sonst füllen sie Plätze und Sockel
      modbusZuletzt[i] = millis();
      int n = c.read(an, 7 > (int)sizeof(an) ? sizeof(an) : 7); if (n < 7) { c.stop(); modbusFehler++; continue; }
      uint16_t len = (an[4] << 8) | an[5];
      if (len < 2 || len > (uint16_t)(sizeof(an) - 6)) { c.stop(); modbusFehler++; modbusKopf++; snprintf(modbusLetzterFehler, sizeof(modbusLetzterFehler), "Kopf: %02X%02X %02X%02X len %u uid %u", an[0], an[1], an[2], an[3], len, an[6]); continue; }   // Kopf unplausibel: Verbindung trennen
      int rest = len - 1; uint32_t t0 = millis();
      while (c.available() < rest && millis() - t0 < 200) delay(2);
      if (c.available() < rest) { c.stop(); modbusFehler++; modbusKopf++; snprintf(modbusLetzterFehler, sizeof(modbusLetzterFehler), "PDU unvollstaendig: %d von %d Byte", c.available(), rest); continue; }
      c.read(an + 7, rest); int na = modbusAntwort(an, 7 + rest, aus); c.write(aus, na); modbusAnfragen++; modbusLetzteMs = millis();
    }
    delay(5);
  }
}
static void modbusStarten() { modbusServer.begin(); modbusServer.setNoDelay(true); xTaskCreate(modbusTask, "modbus", 4096, NULL, 1, NULL); KONSOLE.printf("[MODBUS] TCP-Server Port %u, %u Register\n", MODBUS_PORT, MODBUS_REGISTER); }
// Selbsttest über die eigene IP (Schleife durch den TCP-Stapel): FC3 auf Register 0–14, Ergebnis als Text.
static int modbusSelbsttest(char* out, size_t no) {
  WiFiClient c; bool lo = c.connect(IPAddress(127, 0, 0, 1), MODBUS_PORT, 1500);          // erst Schleifen-Schnittstelle, dann eigene IP
  if (!lo && !c.connect(WiFi.localIP(), MODBUS_PORT, 1500)) return snprintf(out, no, "Selbsttest: keine Verbindung zu Port %u (Server %s)", MODBUS_PORT, (bool)modbusServer ? "lauscht" : "lauscht NICHT");
  uint8_t frage[12] = { 0x12, 0x34, 0, 0, 0, 6, 1, 3, 0, 0, 0, MODBUS_REGISTER }; c.write(frage, 12);
  uint8_t ant[64]; int n = 0; uint32_t t0 = millis();
  while (n < 9 + 2 * MODBUS_REGISTER && millis() - t0 < 1500) { while (c.available() && n < (int)sizeof(ant)) ant[n++] = c.read(); delay(5); }
  c.stop();
  if (n < 9 + 2 * MODBUS_REGISTER) return snprintf(out, no, "Selbsttest: nur %d Byte Antwort", n);
  uint16_t r[MODBUS_REGISTER]; for (int i = 0; i < MODBUS_REGISTER; i++) r[i] = (ant[9 + 2 * i] << 8) | ant[10 + 2 * i];
  return snprintf(out, no, "Selbsttest ok: TID %02X%02X FC%d %d Byte, Messwert %.3f, Winkel %.2f, gueltig %u, Sicherheit %u, Guete %u, Bild %u, Strom %.3f, Dauer %u ms, Ist %u, LED %u",
                  ant[0], ant[1], ant[7], ant[8], registerInFloat(r), registerInFloat(r + 2), r[4], r[5], r[6], r[7], registerInFloat(r + 8), r[10], r[11], r[12]);
}
