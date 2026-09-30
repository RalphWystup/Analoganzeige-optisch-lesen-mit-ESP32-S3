// ---------------------------------------------------------------------------------------------------------------------
//  i_schutz: Selbstschutz (OTA-Rückfall, Absturzzähler → Sicherheitsmodus, Watchdog) und Netzdiagnose, aus Version 1
// ---------------------------------------------------------------------------------------------------------------------
static void schutzStart() {
  esp_reset_reason_t grund = esp_reset_reason(); prefs.begin("zeiger2", false);
  abstuerze = prefs.getUChar("abstuerze", 0);
  if (grund == ESP_RST_PANIC || grund == ESP_RST_TASK_WDT || grund == ESP_RST_INT_WDT || grund == ESP_RST_WDT) abstuerze++; else abstuerze = 0;
  prefs.putUChar("abstuerze", abstuerze);
  if (prefs.getUChar("sicher", 0) || abstuerze >= 3) { sicherheitsmodus = true; prefs.putUChar("sicher", 1); }
  KONSOLE.printf("[SCHUTZ] Resetgrund %d, Abstuerze in Folge %d%s\n", (int)grund, abstuerze, sicherheitsmodus ? " -> SICHERHEITSMODUS (nur WLAN/OTA/Status)" : "");
  esp_task_wdt_config_t wdt = { .timeout_ms = 120000, .idle_core_mask = 0, .trigger_panic = true }; esp_task_wdt_reconfigure(&wdt); esp_task_wdt_add(NULL);
}
static void netzdiagnose() {
  int sk = lwip_socket(AF_INET, SOCK_STREAM, 0); bool sockOk = sk >= 0; if (sockOk) lwip_close(sk);
  KONSOLE.printf("[NETZ] Heap frei %u, groesster Block %u, Sockel-Probe %s, Modbus Anfragen %u Fehler %u voll %u, Stapel min %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), sockOk ? "ok" : "ERSCHOEPFT", (unsigned)modbusAnfragen, (unsigned)modbusFehler, (unsigned)modbusVoll, (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
