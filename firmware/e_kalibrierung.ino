// ---------------------------------------------------------------------------------------------------------------------
//  e_kalibrierung: Version 2 braucht keine Geometrie-Kalibrierung mehr (Ablesung am Schnittpunkt Zeiger/Bogen). Gespeichert
//  werden Messbereich und Einheit sowie die Strichzahlen der Skala als Plausibilität. Bereich aus einem bekannten Prüfstrom
//  (Einpunkt, glatt gerundet: 1, 1,5, 2, 2,5, 3, 4, 5, 6, 8 · 10^k) oder vom Typenschild (/control?var=bereich).
// ---------------------------------------------------------------------------------------------------------------------
static void kalLaden() {
  prefs.begin("zeiger2", false);
  if (prefs.getBytesLength("kal2") == sizeof(KAL)) { prefs.getBytes("kal2", &KAL, sizeof(KAL)); if (KAL.magic != 0x4B414C32) KAL = {}; }
  if (!KAL.ok) { KAL = {}; KAL.magic = 0x4B414C32; KAL.bereich = 1.0f; KAL.nHaupt = 0; KAL.nStriche = 0; strncpy(KAL.einheit, "mA", 7); }
  stromImax = prefs.getFloat("imax", 0.887f);
  KONSOLE.printf("[KAL] %s: Bereich %g %s, %d Hauptstriche, %d Striche\n", KAL.ok ? "geladen" : "keine Kalibrierung", KAL.bereich, KAL.einheit, KAL.nHaupt, KAL.nStriche);
}
static void kalSpeichern() { prefs.putBytes("kal2", &KAL, sizeof(KAL)); }
static float glattRunden(float x, bool& ok) {
  if (x <= 0) { ok = false; return x; }
  float k = powf(10, floorf(log10f(x))); const float stufen[] = {1, 1.5f, 2, 2.5f, 3, 4, 5, 6, 8, 10}; float best = x; float bd = 1e9;
  for (float s : stufen) { float v = s * k; if (fabsf(v - x) < bd) { bd = fabsf(v - x); best = v; } }
  ok = bd / x < 0.04f; return ok ? best : x;
}
// Wird im Bildtakt ausgeführt (kalAnfrage): Messbereich aus einem bekannten Prüfstrom I > 0: Ablesung f (Anteil des Skalenwegs
// am Schnittpunkt Zeiger/Bogen) → Bereich = I / f, auf glatte Werte gerundet. Ohne Prüfstrom: /control?var=bereich (Typenschild).
static void kalFesthalten(float wert) {
  if (!SKALA.ok || !ZEIGER.linie.ok) { snprintf(kalAntwort, sizeof(kalAntwort), "Skala (%s) oder Zeigergerade (%s) fehlt – Bild pruefen", SKALA.ok ? "ok" : "fehlt", ZEIGER.linie.ok ? "ok" : "fehlt"); return; }
  if (wert <= 0) { snprintf(kalAntwort, sizeof(kalAntwort), "Nullpunkt: Ablesung %.4f des Skalenwegs (Sollwert 0) – zur Kontrolle, kein Kalibrierschritt", wertAusWinkel(SKALA, ZEIGER.winkel, 1.0f)); return; }
  float anteil = wertAusWinkel(SKALA, ZEIGER.winkel, 1.0f);
  if (isnan(anteil) || anteil < 0.05f) { snprintf(kalAntwort, sizeof(kalAntwort), "Zeiger zu nahe am Anfang (Anteil %.3f) – groesseren Strom waehlen", anteil); return; }
  bool glatt = false; float bereich = glattRunden(wert / anteil, glatt);
  KAL.bereich = bereich; KAL.nHaupt = SKALA.nHaupt; KAL.nStriche = SKALA.nStriche; KAL.ok = true; kalSpeichern(); wertRingN = 0;
  snprintf(kalAntwort, sizeof(kalAntwort), "Kalibriert: %g %s bei Anteil %.4f -> Bereich %g %s (%s), %d Hauptstriche, %d Striche gemerkt", wert, KAL.einheit, anteil, KAL.bereich, KAL.einheit, glatt ? "glatt gerundet" : "NICHT glatt – Prüfstrom/Skala pruefen", KAL.nHaupt, KAL.nStriche);
  KONSOLE.printf("[KAL] %s\n", kalAntwort);
}
static void kalLoeschen() { KAL.ok = false; kalpunkte[0].ok = kalpunkte[1].ok = false; KAL.nHaupt = 0; KAL.nStriche = 0; kalSpeichern(); wertRingN = 0; }
