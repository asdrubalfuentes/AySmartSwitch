#include "servidor.h"
#include "config.h"
#include "certs.h"
#include "store.h"
#include "net.h"
#include "canales.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Habla con monitor-api por HTTPS validando el certificado contra ISRG Root X1.
// Necesita la hora correcta (si no, el certificado parece "aun no valido").
static int pedir(const char* metodo, const String& ruta, const String& cuerpo, String& respuesta, bool conAuth) {
  if (!horaOk()) netSincronizarHora(15000);
  if (!horaOk()) { respuesta = "sin hora"; return -1; }

  WiFiClientSecure cliente;
  cliente.setCACert(ISRG_ROOT_X1);
  cliente.setTimeout(15);
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(cliente, String(SERVER_BASE) + ruta)) { respuesta = "no se pudo abrir la conexion"; return -1; }
  if (conAuth) http.setAuthorization(cfg.mqttUser, cfg.mqttPass);
  http.addHeader("Content-Type", "application/json");
  int codigo = !strcmp(metodo, "POST") ? http.POST(cuerpo) : http.GET();
  respuesta = codigo > 0 ? http.getString() : http.errorToString(codigo);
  http.end();
  return codigo;
}

static String mensajeDe(const String& cuerpo) {
  JsonDocument doc;
  if (!deserializeJson(doc, cuerpo) && doc["error"].is<const char*>()) return String(doc["error"].as<const char*>());
  return cuerpo.substring(0, 120);
}

// Aplica la configuracion que entrega el servidor (adhesion o refresco).
static bool aplicar(JsonDocument& doc, bool conClave) {
  JsonObject mqtt = doc["mqtt"];
  if (mqtt.isNull() || !doc["canales"].is<JsonArray>()) return false;

  strncpy(cfg.mqttHost, mqtt["host"] | "emqx.aysafi.com", sizeof(cfg.mqttHost) - 1);
  cfg.mqttPort = mqtt["port"] | 1883;
  cfg.mqttTls = mqtt["tls"] | false;
  strncpy(cfg.mqttUser, mqtt["user"] | (const char*)deviceId, sizeof(cfg.mqttUser) - 1);
  if (conClave && mqtt["pass"].is<const char*>()) strncpy(cfg.mqttPass, mqtt["pass"].as<const char*>(), sizeof(cfg.mqttPass) - 1);
  strncpy(cfg.base, mqtt["base"] | "", sizeof(cfg.base) - 1);
  strncpy(cfg.otaOwner, doc["ota"]["owner"] | OTA_OWNER_DEFAULT, sizeof(cfg.otaOwner) - 1);
  strncpy(cfg.otaRepo, doc["ota"]["repo"] | OTA_REPO_DEFAULT, sizeof(cfg.otaRepo) - 1);
  strncpy(cfg.cfgv, doc["cfgv"] | "", sizeof(cfg.cfgv) - 1);
  cfg.metricasS = doc["metricasS"] | 60;
  if (cfg.metricasS < 15) cfg.metricasS = 15;
  cfg.enrolled = true;
  cfg.code[0] = 0;
  storeSaveEnroll();

  String canales;
  serializeJson(doc["canales"], canales);
  storeSaveCanales(canales);
  return true;
}

RespuestaServidor servidorEnrolar(String& error) {
  JsonDocument cuerpo;
  cuerpo["codigo"] = cfg.code;
  cuerpo["deviceId"] = deviceId;
  cuerpo["fw"] = FW_VERSION;
  cuerpo["modelo"] = MODEL_NAME;
  String json, resp;
  serializeJson(cuerpo, json);

  int codigo = pedir("POST", "/device/enroll", json, resp, false);
  if (codigo < 0) { error = resp; return RespuestaServidor::SinRed; }
  if (codigo != 200) { error = mensajeDe(resp); return RespuestaServidor::Rechazado; }

  JsonDocument doc;
  if (deserializeJson(doc, resp) || !aplicar(doc, true)) { error = "respuesta invalida del servidor"; return RespuestaServidor::Rechazado; }
  return RespuestaServidor::Ok;
}

RespuestaServidor servidorRefrescarConfig(String& error) {
  String resp;
  int codigo = pedir("GET", "/device/config", "", resp, true);
  if (codigo < 0) { error = resp; return RespuestaServidor::SinRed; }
  if (codigo == 401) { error = "el servidor ya no reconoce a este equipo"; return RespuestaServidor::Desvinculado; }
  if (codigo != 200) { error = mensajeDe(resp); return RespuestaServidor::Rechazado; }

  JsonDocument doc;
  if (deserializeJson(doc, resp) || !aplicar(doc, false)) { error = "respuesta invalida del servidor"; return RespuestaServidor::Rechazado; }
  return RespuestaServidor::Ok;
}
