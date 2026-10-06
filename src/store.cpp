#include "store.h"
#include "config.h"
#include <Preferences.h>
#include <esp_system.h>

Cfg cfg;
char deviceId[13] = "";

static Preferences prefs;
static const char* NS = "ays";

static void getStr(const char* key, char* out, size_t n, const char* def = "") {
  String v = prefs.getString(key, def);
  strncpy(out, v.c_str(), n - 1);
  out[n - 1] = 0;
}

void storeInit() {
  // El id sale de la MAC de fabrica: no cambia aunque se borre todo.
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);   // lee la MAC del efuse; no depende de que el WiFi ya este iniciado
  snprintf(deviceId, sizeof(deviceId), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  prefs.begin(NS, false);
  memset(&cfg, 0, sizeof(cfg));
  getStr("ssid", cfg.ssid, sizeof(cfg.ssid));
  getStr("pass", cfg.pass, sizeof(cfg.pass));
  getStr("code", cfg.code, sizeof(cfg.code));
  cfg.enrolled = prefs.getBool("enrolled", false);
  getStr("mhost", cfg.mqttHost, sizeof(cfg.mqttHost));
  cfg.mqttPort = prefs.getUShort("mport", 1883);
  cfg.mqttTls = prefs.getBool("mtls", false);
  getStr("muser", cfg.mqttUser, sizeof(cfg.mqttUser));
  getStr("mpass", cfg.mqttPass, sizeof(cfg.mqttPass));
  getStr("base", cfg.base, sizeof(cfg.base));
  getStr("oowner", cfg.otaOwner, sizeof(cfg.otaOwner), OTA_OWNER_DEFAULT);
  getStr("orepo", cfg.otaRepo, sizeof(cfg.otaRepo), OTA_REPO_DEFAULT);
  getStr("cfgv", cfg.cfgv, sizeof(cfg.cfgv));
  cfg.metricasS = prefs.getUShort("metS", 60);
  if (cfg.metricasS < 15) cfg.metricasS = 15;
}

void storeSaveWifi(const char* ssid, const char* pass) {
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  strncpy(cfg.ssid, ssid, sizeof(cfg.ssid) - 1);
  strncpy(cfg.pass, pass, sizeof(cfg.pass) - 1);
}

void storeSaveCode(const char* code) {
  prefs.putString("code", code);
  strncpy(cfg.code, code, sizeof(cfg.code) - 1);
  cfg.code[sizeof(cfg.code) - 1] = 0;
}

void storeSaveEnroll() {
  prefs.putBool("enrolled", cfg.enrolled);
  prefs.putString("mhost", cfg.mqttHost);
  prefs.putUShort("mport", cfg.mqttPort);
  prefs.putBool("mtls", cfg.mqttTls);
  prefs.putString("muser", cfg.mqttUser);
  prefs.putString("mpass", cfg.mqttPass);
  prefs.putString("base", cfg.base);
  prefs.putString("oowner", cfg.otaOwner);
  prefs.putString("orepo", cfg.otaRepo);
  prefs.putString("cfgv", cfg.cfgv);
  prefs.putUShort("metS", cfg.metricasS);
  prefs.putString("code", cfg.code);
}

void storeSaveCanales(const String& json) { prefs.putString("canales", json); }
String storeLoadCanales() { return prefs.getString("canales", ""); }
void storeSaveCiclos(const String& json) { prefs.putString("ciclos", json); }
String storeLoadCiclos() { return prefs.getString("ciclos", ""); }

void storeFactoryReset() {
  prefs.clear();
  delay(200);
  ESP.restart();
}
