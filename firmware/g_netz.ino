// ---------------------------------------------------------------------------------------------------------------------
//  g_netz: WLAN, OTA, Webseite, /status, /control, /kal, /kal_reset, /strom, /capture, Stream, Sockelbudget (aus V1)
// ---------------------------------------------------------------------------------------------------------------------
static httpd_handle_t seiten_httpd = NULL, stream_httpd = NULL;
static const char SEITE[] = R"HTML(<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Analoganzeige V2</title>
<style>body{font-family:system-ui,sans-serif;margin:0;background:#f4f6f7;color:#1e2a30}header{background:#0e2a35;color:#fff;padding:12px 18px}main{display:grid;grid-template-columns:1fr 1fr;gap:14px;padding:14px;max-width:1300px;margin:auto}
.card{background:#fff;border-radius:10px;padding:12px 16px;box-shadow:0 1px 3px rgba(0,0,0,.08)}.wert{font-size:56px;font-weight:600}.klein{font-size:13px;color:#5c6b74}img{width:100%;border-radius:8px}label{margin-right:12px}
.ok{color:#0a8f3f}.warn{color:#8c1d1d}@media(max-width:900px){main{grid-template-columns:1fr}}</style></head><body>
<header><h1 style="margin:0;font-size:18px">Analoganzeige lesen · Version 2 · Skala in jedem Bild</h1><div class="klein" id="kopf" style="color:#cfe">verbinde …</div></header><main>
<section class="card"><h2>Messwert</h2><div class="wert" id="wert">–</div><div class="klein" id="zeile"></div><div class="klein" id="skala"></div></section>
<section class="card"><h2>Livebild mit Skala und Zeiger</h2><img id="bild" src="/capture"><div class="klein">orange Bogen · cyan Teilstriche · magenta Hauptstriche · grün Ziffern · blau Zeiger vom Drehpunkt</div>
<div><label><input type="checkbox" id="ov" onchange="ctl('overlay',this.checked?1:0)" checked> Einzeichnung</label><label>Ansicht <select id="an" onchange="ctl('ansicht',this.value)"><option value="0">Kamera</option><option value="1">Rotbild</option><option value="2">Graubild</option></select></label></div></section>
<section class="card"><h2>Kalibrierung</h2><div class="klein">Zwei Ausschlaege festhalten (Pruefstrom hier im Labor); im Einbau: Betriebskalibrierung aus der Zeigerbewegung.</div>
<div><label>Strom <input id="st" type="number" step="0.01" style="width:80px"></label><button onclick="fetch('/strom?wert='+$('st').value).then(r=>r.text()).then(t=>$('m').textContent=t)">Pruefstrom setzen</button>
<button onclick="fetch('/kal?wert='+$('st').value).then(r=>r.text()).then(t=>$('m').textContent=t)">Ausschlag festhalten</button><button onclick="fetch('/kal_reset').then(r=>r.text()).then(t=>$('m').textContent=t)">Kalibrierung loeschen</button></div>
<div class="klein" id="m"></div><div class="klein" id="kal"></div></section>
<section class="card"><h2>Licht</h2><div class="klein" id="licht"></div><div><label>Belichtung <input id="aec" type="number" style="width:70px" onchange="ctl('aec',this.value)"></label><label>Verstaerkung <input id="gain" type="number" style="width:60px" onchange="ctl('gain',this.value)"></label>
<label><input type="checkbox" id="rg" onchange="ctl('lichtregel',this.checked?1:0)"> Regelung auf Weiss <input id="rz" type="number" style="width:60px" onchange="ctl('regelziel',this.value)"></label></div></section>
</main><script>
const $=id=>document.getElementById(id);function ctl(v,w){fetch('/control?var='+v+'&val='+w)}
async function tick(){try{const j=await (await fetch('/status')).json();$('kopf').textContent=j.version+' · Bild '+j.bilder+' · '+j.dauer_ms+' ms (Skala '+j.skala.ms+', Zeiger '+j.zeiger_ms+') · '+j.ip;
const s=j.skala;$('skala').innerHTML='Skala: '+(s.ok?'<span class=ok>gueltig</span>':'<span class=warn>nicht gueltig</span>')+' · Bogen r '+s.r.toFixed(0)+' ('+s.inlier+' Punkte) · '+s.striche+' Striche, '+s.haupt+' Hauptstriche, '+s.ziffern+' Ziffern · Abstand '+s.pitch.toFixed(2)+'° ± '+(100*s.regel).toFixed(1)+' % · Guete '+s.guete.toFixed(2);
$('wert').innerHTML=j.gueltig?j.wert_median.toFixed(3)+'<small style="font-size:22px"> '+j.einheit+'</small>':'<span class=warn style="font-size:34px">unsicher</span>';
$('zeile').textContent='Zeiger '+(j.winkel==null?'–':j.winkel.toFixed(2)+'°')+' · Gerade '+j.zeiger_punkte+' Punkte · Pruefstrom '+j.strom.soll+' '+j.einheit;
$('kal').textContent=j.kalibriert?('Versatz ('+j.kal.vx.toFixed(3)+', '+j.kal.vy.toFixed(3)+') r · Bereich '+j.kal.bereich+' '+j.einheit+' · '+j.kal.nhaupt+' Hauptstriche'):'nicht kalibriert';
$('licht').textContent='Weiss p95 '+j.licht.weiss+' · Schwarz p5 '+j.licht.schwarz+' · Saettigung '+(100*j.licht.saett).toFixed(1)+' % · Belichtung '+j.licht.aec+' · Verstaerkung '+j.licht.gain+' · LED '+j.led[0];
if(document.activeElement.id!='aec')$('aec').value=j.licht.aec;if(document.activeElement.id!='gain')$('gain').value=j.licht.gain;$('rg').checked=j.licht.regel;if(document.activeElement.id!='rz')$('rz').value=j.licht.ziel;
}catch(e){$('kopf').textContent='keine Verbindung';}$('bild').src='/capture?t='+Date.now();}
setInterval(tick,1500);tick();</script></body></html>)HTML";
static esp_err_t seite_handler(httpd_req_t* req) { httpd_resp_set_type(req, "text/html; charset=utf-8"); return httpd_resp_send(req, SEITE, HTTPD_RESP_USE_STRLEN); }
static esp_err_t status_handler(httpd_req_t* req) {
  static char buf[3000]; int n = 0;
  if (sicherheitsmodus) { n = snprintf(buf, sizeof(buf), "{\"board\":\"Freenove ESP32-S3\",\"version\":\"%s\",\"sicherheitsmodus\":true,\"abstuerze\":%d,\"ip\":\"%s\"}", VERSION_TEXT, abstuerze, WiFi.localIP().toString().c_str()); httpd_resp_set_type(req, "application/json"); return httpd_resp_send(req, buf, n); }
  const Geometrie& s = SKALA;
  n += snprintf(buf + n, sizeof(buf) - n, "{\"board\":\"Freenove ESP32-S3\",\"version\":\"%s\",\"ip\":\"%s\",\"bilder\":%lu,\"dauer_ms\":%lu,\"zeiger_ms\":%lu,\"jpeg_ms\":%lu,\"einheit\":\"%s\",\"kalibriert\":%s,",
                VERSION_TEXT, WiFi.localIP().toString().c_str(), (unsigned long)bildzaehler, (unsigned long)dauerMs, (unsigned long)dauerZeigerMs, (unsigned long)dauerJpegMs, KAL.einheit, KAL.ok ? "true" : "false");
  n += snprintf(buf + n, sizeof(buf) - n, "\"skala\":{\"ok\":%s,\"cx\":%.1f,\"cy\":%.1f,\"r\":%.1f,\"a0\":%.1f,\"a1\":%.1f,\"inlier\":%d,\"striche\":%d,\"haupt\":%d,\"ziffern\":%d,\"pitch\":%.3f,\"regel\":%.3f,\"guete\":%.2f,\"ms\":%lu,\"ok_zaehler\":%lu,\"fehl_zaehler\":%lu,\"schnell\":%lu,\"voll\":%lu,\"ueberbel\":%lu},",
                s.ok ? "true" : "false", s.cx, s.cy, s.r, s.a0, s.a1, s.inlier, s.nStriche, s.nHaupt, s.nZiffern, s.pitch, s.regel, s.guete, (unsigned long)s.ms, (unsigned long)skalaOkZaehler, (unsigned long)skalaFehlZaehler, (unsigned long)schnelleSuchen, (unsigned long)volleSuchen, (unsigned long)ueberbelichtet);
  n += snprintf(buf + n, sizeof(buf) - n, "\"hauptwinkel\":["); for (int k = 0; k < s.nHaupt; k++) n += snprintf(buf + n, sizeof(buf) - n, "%s%.2f", k ? "," : "", s.haupt[k]); n += snprintf(buf + n, sizeof(buf) - n, "],");
  if (ZEIGER.ok) n += snprintf(buf + n, sizeof(buf) - n, "\"gueltig\":true,\"wert\":%.3f,\"wert_median\":%.3f,\"winkel\":%.2f,", ZEIGER.wert, wertMedian, ZEIGER.winkel);
  else n += snprintf(buf + n, sizeof(buf) - n, "\"gueltig\":false,\"wert\":null,\"wert_median\":%s,\"winkel\":%s,", isnan(wertMedian) ? "null" : "0", ZEIGER.linie.ok ? "0" : "null");
  if (!ZEIGER.ok && !isnan(wertMedian)) { n -= 0; }
  n += snprintf(buf + n, sizeof(buf) - n, "\"zeiger_punkte\":%d,\"sicherheit\":%.0f,\"kal\":{\"vx\":%.4f,\"vy\":%.4f,\"bereich\":%g,\"nhaupt\":%d,\"nstriche\":%d},\"kal_antwort\":\"%s\",",
                ZEIGER.linie.n, ZEIGER.sicherheit, KAL.vx, KAL.vy, KAL.bereich, KAL.nHaupt, KAL.nStriche, kalAntwort);
  n += snprintf(buf + n, sizeof(buf) - n, "\"licht\":{\"weiss\":%.0f,\"schwarz\":%.0f,\"saett\":%.3f,\"aec\":%d,\"gain\":%d,\"regel\":%s,\"ziel\":%.0f,\"ledmax\":%d},\"led\":[%d,%d,%d],\"overlay\":%s,\"ansicht\":%d,\"stream\":\"%s\",",
                weiss95, schwarz5, saettigung, regelAec, regelGain, regelAn ? "true" : "false", regelZiel, (int)regelLedMax, ledR, ledG, ledB, overlay ? "true" : "false", (int)ansicht, streamKlein ? "320x240" : "640x480");
  n += snprintf(buf + n, sizeof(buf) - n, "\"zeiten\":{\"grau\":%lu,\"duenn9\":%lu,\"pruef\":%lu,\"striche\":%lu,\"ziffern\":%lu,\"rot\":%lu,\"zduenn\":%lu,\"zkand\":%lu,\"zransac\":%lu},\"strom\":{\"soll\":%.3f,\"imax\":%.3f,\"pwm_ok\":%s},\"abstuerze\":%d,\"heap\":%u,\"modbus\":{\"anfragen\":%u,\"fehler\":%u,\"voll\":%u,\"verworfen\":%u,\"schreiben\":%s,\"letzter\":\"%s\",\"letzter_fehler\":\"%s\"}}",
                (unsigned long)tGrau, (unsigned long)tDuenn, (unsigned long)tPruef, (unsigned long)tStriche, (unsigned long)tZiffern, (unsigned long)tRot, (unsigned long)tZDuenn, (unsigned long)tZKand, (unsigned long)tZRansac, stromSoll, stromImax, pwmOk ? "true" : "false", (int)abstuerze, (unsigned)ESP.getFreeHeap(), (unsigned)modbusAnfragen, (unsigned)modbusFehler, (unsigned)modbusVoll, (unsigned)modbusVerworfen, modbusSchreiben ? "true" : "false", modbusLetzter, modbusLetzterFehler);
  httpd_resp_set_type(req, "application/json"); httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"); return httpd_resp_send(req, buf, n);
}
static esp_err_t kal_handler(httpd_req_t* req) {
  char query[64] = {0}, wert[24] = {0};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK || httpd_query_key_value(query, "wert", wert, sizeof(wert)) != ESP_OK) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "wert fehlt"); return ESP_FAIL; }
  for (char* c = wert; *c; c++) if (*c == ',') *c = '.';
  kalWert = atof(wert); kalFertig = false; kalAnfrage = true;
  for (int i = 0; i < 160 && !kalFertig; i++) vTaskDelay(pdMS_TO_TICKS(50));
  if (!kalFertig) { httpd_resp_sendstr(req, "Keine Antwort der Bildverarbeitung"); return ESP_OK; }
  httpd_resp_sendstr(req, kalAntwort); return ESP_OK;
}
static esp_err_t kal_reset_handler(httpd_req_t* req) { kalLoeschen(); httpd_resp_sendstr(req, "Kalibrierung geloescht."); return ESP_OK; }
static esp_err_t strom_handler(httpd_req_t* req) {
  char query[64] = {0}, val[24] = {0}; char antwort[120];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK || httpd_query_key_value(query, "wert", val, sizeof(val)) != ESP_OK) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "wert fehlt"); return ESP_FAIL; }
  for (char* c = val; *c; c++) if (*c == ',') *c = '.'; stromSetzen(atof(val));
  snprintf(antwort, sizeof(antwort), "Pruefstrom %.3f %s (Aussteuerung %.1f %%)", stromSoll, KAL.einheit, 100.0f * stromSoll / stromImax); httpd_resp_sendstr(req, antwort); return ESP_OK;
}
static esp_err_t control_handler(httpd_req_t* req) {
  char query[96] = {0}, var[20] = {0}, val[24] = {0};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK || httpd_query_key_value(query, "var", var, sizeof(var)) != ESP_OK || httpd_query_key_value(query, "val", val, sizeof(val)) != ESP_OK) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "var/val fehlen"); return ESP_FAIL; }
  for (char* c = val; *c; c++) if (*c == ',' && !strcmp(var, "led")) {} else if (*c == ',') *c = '.';
  int v = atoi(val);
  if (!strcmp(var, "sicher")) { prefs.putUChar("sicher", v ? 1 : 0); prefs.putUChar("abstuerze", 0); httpd_resp_sendstr(req, v ? "Sicherheitsmodus beim naechsten Start" : "Sicherheitsmodus aufgehoben, Neustart"); if (!v) { delay(300); ESP.restart(); } return ESP_OK; }
  if (!strcmp(var, "neustart")) { httpd_resp_sendstr(req, "Neustart"); delay(300); ESP.restart(); return ESP_OK; }
  if (sicherheitsmodus) { httpd_resp_sendstr(req, "Sicherheitsmodus: nur var=sicher"); return ESP_OK; }
  sensor_t* s = esp_camera_sensor_get();
  if (!strcmp(var, "overlay")) overlay = v != 0;
  else if (!strcmp(var, "ansicht")) ansicht = v < 0 || v > 2 ? 0 : v;
  else if (!strcmp(var, "stream")) { streamKlein = v == 0; prefs.putUChar("streamklein", streamKlein); }
  else if (!strcmp(var, "quality")) jpegQualitaet = constrain(v, 10, 95);
  else if (!strcmp(var, "hmirror")) { if (s) s->set_hmirror(s, v); prefs.putUChar("hmirror", v ? 1 : 0); }
  else if (!strcmp(var, "vflip")) { if (s) s->set_vflip(s, v); prefs.putUChar("vflip", v ? 1 : 0); }
  else if (!strcmp(var, "bytetausch")) { byteTausch = v != 0; prefs.putUChar("bytetausch", byteTausch); }
  else if (!strcmp(var, "aec")) { regelAn = false; regelAec = constrain(v, 20, 1200); regelKamera(); }
  else if (!strcmp(var, "gain")) { regelAn = false; regelGain = constrain(v, 0, 30); regelKamera(); }
  else if (!strcmp(var, "lichtregel")) { regelAn = v != 0; prefs.putUChar("regelan", regelAn); }
  else if (!strcmp(var, "regelziel")) { regelZiel = constrain(atof(val), 100.0f, 245.0f); prefs.putFloat("regelziel", regelZiel); }
  else if (!strcmp(var, "regelledmax")) { regelLedMax = constrain(v, 0, 255); prefs.putUChar("regelledmax", regelLedMax); }
  else if (!strcmp(var, "led")) { int r = 0, g = 0, b = 0; sscanf(val, "%d,%d,%d", &r, &g, &b); ledSetzen(constrain(r, 0, 255), constrain(g, 0, 255), constrain(b, 0, 255)); regelLed = r; }
  else if (!strcmp(var, "imax")) { float f = atof(val); if (f > 0) { stromImax = f; prefs.putFloat("imax", stromImax); } }
  else if (!strcmp(var, "bereich")) { float f = atof(val); if (f > 0) { KAL.bereich = f; KAL.ok = true; kalSpeichern(); } }        // Messbereich vom Typenschild (Einbau ohne Pruefstrom)
  else if (!strcmp(var, "einheit")) { strncpy(KAL.einheit, val, 7); KAL.einheit[7] = 0; kalSpeichern(); }
  else if (!strcmp(var, "modbusschreiben")) modbusSchreiben = v != 0;
  else if (!strcmp(var, "modbustest")) { char t[240]; modbusSelbsttest(t, sizeof(t)); httpd_resp_sendstr(req, t); return ESP_OK; }
  else if (!strcmp(var, "pintest")) { pinTest = v; if (v >= 0) { sigmaDeltaDetach(STROM_PIN); pinMode(STROM_PIN, OUTPUT); digitalWrite(STROM_PIN, v ? HIGH : LOW); } else { pwmOk = quelleEinrichten(stromQuelle); stromSetzen(stromSoll); } }
  else { httpd_resp_sendstr(req, "unbekannt"); return ESP_OK; }
  httpd_resp_sendstr(req, "OK"); return ESP_OK;
}
static esp_err_t capture_handler(httpd_req_t* req) {
  size_t len; uint8_t* k = jpegKopie(len); if (!k) { httpd_resp_send_500(req); return ESP_FAIL; }
  httpd_resp_set_type(req, "image/jpeg"); httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"); esp_err_t r = httpd_resp_send(req, (const char*)k, len); free(k); return r;
}
static esp_err_t stream_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame"); uint32_t letzter = 0; char kopf[96];
  while (true) {
    while (bildzaehler == letzter) vTaskDelay(pdMS_TO_TICKS(20)); letzter = bildzaehler;
    size_t len; uint8_t* k = jpegKopie(len); if (!k) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    int n = snprintf(kopf, sizeof(kopf), "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", (unsigned)len);
    esp_err_t r = httpd_resp_send_chunk(req, kopf, n); if (r == ESP_OK) r = httpd_resp_send_chunk(req, (const char*)k, len); free(k); if (r != ESP_OK) break;
  }
  return ESP_OK;
}
static void netzStart(bool nurSchutz) {
  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.begin(WIFI_SSID, WIFI_PASSWORT);
  KONSOLE.print("[WLAN] Verbinde"); for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) { delay(500); KONSOLE.print('.'); esp_task_wdt_reset(); } KONSOLE.println();
  if (WiFi.status() == WL_CONNECTED) KONSOLE.printf("[OK] WLAN %s, IP %s\n", WIFI_SSID, WiFi.localIP().toString().c_str()); else KONSOLE.println("[WARNUNG] Kein WLAN; Wiederverbindung in loop()");
  ArduinoOTA.setHostname(OTA_HOSTNAME); ArduinoOTA.onStart([]() { KONSOLE.println("[OTA] Update beginnt"); }); ArduinoOTA.onEnd([]() { KONSOLE.println("[OTA] Update fertig, Neustart"); }); ArduinoOTA.begin();
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG(); cfg.server_port = 80; cfg.ctrl_port = 32768; cfg.max_uri_handlers = 10; cfg.stack_size = 12288; cfg.max_open_sockets = 3; cfg.lru_purge_enable = true;
  httpd_uri_t u_seite = { .uri = "/", .method = HTTP_GET, .handler = seite_handler, .user_ctx = NULL };
  httpd_uri_t u_status = { .uri = "/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL };
  httpd_uri_t u_control = { .uri = "/control", .method = HTTP_GET, .handler = control_handler, .user_ctx = NULL };
  httpd_uri_t u_capture = { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler, .user_ctx = NULL };
  httpd_uri_t u_kal = { .uri = "/kal", .method = HTTP_GET, .handler = kal_handler, .user_ctx = NULL };
  httpd_uri_t u_kalr = { .uri = "/kal_reset", .method = HTTP_GET, .handler = kal_reset_handler, .user_ctx = NULL };
  httpd_uri_t u_strom = { .uri = "/strom", .method = HTTP_GET, .handler = strom_handler, .user_ctx = NULL };
  if (httpd_start(&seiten_httpd, &cfg) == ESP_OK) { httpd_register_uri_handler(seiten_httpd, &u_seite); httpd_register_uri_handler(seiten_httpd, &u_status); httpd_register_uri_handler(seiten_httpd, &u_control);
    if (!nurSchutz) { httpd_register_uri_handler(seiten_httpd, &u_capture); httpd_register_uri_handler(seiten_httpd, &u_kal); httpd_register_uri_handler(seiten_httpd, &u_kalr); httpd_register_uri_handler(seiten_httpd, &u_strom); } }
  if (!nurSchutz) { cfg.server_port = 81; cfg.ctrl_port = 32769; cfg.max_open_sockets = 2; httpd_uri_t u_stream = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL }; if (httpd_start(&stream_httpd, &cfg) == ESP_OK) httpd_register_uri_handler(stream_httpd, &u_stream); modbusStarten(); }
}
static void netzPflege() {
  static uint32_t letzterCheck = 0, letzterStatus = 0;
  if (millis() - letzterCheck > 5000) { letzterCheck = millis(); if (WiFi.status() != WL_CONNECTED) { KONSOLE.println("[WLAN] Verbindung verloren, Reconnect …"); WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASSWORT); } }
  if (millis() - letzterStatus > 10000) { letzterStatus = millis(); netzdiagnose();
    KONSOLE.printf("[STATUS] Skala %s (%d Striche, %d Haupt, Guete %.2f, %lu ms) · Zeiger %s Wert %.3f %s (%d Punkte) · Weiss %.0f aec %d gain %d · %lu ms/Bild\n", SKALA.ok ? "ok" : "FEHLT", SKALA.nStriche, SKALA.nHaupt, SKALA.guete, (unsigned long)dauerSkalaMs, ZEIGER.ok ? "ok" : "unsicher", ZEIGER.wert, KAL.einheit, ZEIGER.linie.n, weiss95, regelAec, regelGain, (unsigned long)dauerMs); }
}
