// ---------------------------------------------------------------------------------------------------------------------
//  j_verarbeite: Bildtakt – Kanäle, Skala, Zeiger, Gültigkeit, Regelung, Kalibrierschritt, Einzeichnung, JPEG
// ---------------------------------------------------------------------------------------------------------------------
static void verarbeite(camera_fb_t* fb) {
  uint32_t t0 = millis();
  uint32_t tg = millis(); graubild(fb->buf); tGrau = millis() - tg;
  uint32_t ts = millis(); Geometrie g = skalaErkennen(); dauerSkalaMs = millis() - ts;
  if (g.ok) { SKALA = g; SKALA_GUT = g; skalaOkZaehler++; } else { SKALA = g; skalaFehlZaehler++; }
  const Geometrie& G = g.ok ? g : SKALA_GUT;           // Zeiger nur gegen eine gültige Skala messen (aktuell oder zuletzt gültig)
  photometrie(G);
  if (g.ok && saettigung > 0.03f) { g.ok = false; SKALA.ok = false; skalaOkZaehler--; skalaFehlZaehler++; ueberbelichtet++; }   // übersteuertes Bild: keine Skala, kein Wert (16.09.)
  weissabgleich(fb->buf, G); tg = millis(); rotbild(fb->buf); tRot = millis() - tg;
  Zeiger z = {};
  if (G.inlier) { z = zeigerMessen(G); }
  z.ok = z.ok && g.ok;                                  // Zweistufige Gültigkeit: Skala in DIESEM Bild gültig UND Zeiger gültig
  ZEIGER = z;
  if (z.ok) { wertRing[wertRingI] = z.wert; wertRingI = (wertRingI + 1) % 5; if (wertRingN < 5) wertRingN++; float tmp[5]; memcpy(tmp, wertRing, sizeof(float) * wertRingN); std::sort(tmp, tmp + wertRingN); wertMedian = tmp[wertRingN / 2]; }
  regelung();
  if (kalAnfrage) { kalAnfrage = false; kalFesthalten(kalWert); kalFertig = true; }
  if (overlay) {
    if (ansicht == 1 || ansicht == 2) { uint16_t* q = (uint16_t*)fb->buf; const uint8_t* src = ansicht == 1 ? ROT : GRAU; for (int i = 0; i < W * H; i++) { int v = src[i]; q[i] = px565(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3)); } }
    skalaZeichnen(fb->buf, g.inlier ? g : SKALA_GUT); zeigerZeichnen(fb->buf, G, z);
  }
  nurJpeg(fb); bildzaehler++; dauerMs = millis() - t0;
}
