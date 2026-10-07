#include "enlace_mqtt.h"
#include "config.h"
#include "certs.h"
#include "store.h"
#include "net.h"
#include "canales.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_system.h>

// Protocolo v2 (ver README): todo cuelga de cfg.base = aysafi/<org>/<deviceId>
//   status (retenido, LWT)  metrics  ch/<id>/state (retenido)  ch/<id>/ack  ch/<id>/evt
//   ch/<id>/cmd (entrada)   cfg (entrada, retenido)

uint32_t mqttReconexiones = 0;

static WiFiClient plano;
static WiFiClientSecure seguro;
static PubSubClient mqtt;
static bool preparado = false;
static bool primeraConexion = true;
static uint32_t ultimoIntento = 0;
static uint32_t espera = 5000;
static uint32_t ultimoStatus = 0;
static uint32_t ultimaMetrica = 0;
static bool cfgNueva = false;

static String tema(const char* resto) { return String(cfg.base) + "/" + resto; }
static String temaCanal(const char* id, const char* que) { return String(cfg.base) + "/ch/" + id + "/" + que; }

static const char* razonReinicio() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    default: return "otro";
  }
}

static void publicar(const String& t, const String& payload, bool retenido = false) {
  if (!mqtt.connected()) { Serial.printf("[mqtt] no publica %s: sin conexion\n", t.c_str()); return; }
  bool ok = mqtt.publish(t.c_str(), payload.c_str(), retenido);
  Serial.printf("[mqtt] publica %s (%u bytes) -> %s\n", t.c_str() + strlen(cfg.base), (unsigned)payload.length(), ok ? "ok" : "FALLO");
}

static void publicarStatus() {
  JsonDocument d;
  d["online"] = true;
  d["fw"] = FW_VERSION;
  d["ip"] = WiFi.localIP().toString();
  d["rssi"] = WiFi.RSSI();
  d["uptime_s"] = millis() / 1000;
  d["cfgv"] = cfg.cfgv;
  String s;
  serializeJson(d, s);
  publicar(tema("status"), s, true);
  ultimoStatus = millis();
}

static void publicarMetricas() {
  JsonDocument d;
  d["fw"] = FW_VERSION;
  d["uptime_s"] = millis() / 1000;
  d["rssi"] = WiFi.RSSI();
  d["heap_free"] = ESP.getFreeHeap();
  d["heap_min"] = ESP.getMinFreeHeap();
  d["reset_reason"] = razonReinicio();
  d["wifi_reconnects"] = wifiReconexiones;
  d["mqtt_reconnects"] = mqttReconexiones;
  canalesCiclosJson(d["ciclos"].to<JsonObject>());
  String s;
  serializeJson(d, s);
  publicar(tema("metrics"), s);
  ultimaMetrica = millis();
}

void mqttPublicarEstado(const Canal& c, const char* valor) {
  JsonDocument d;
  d["v"] = valor;
  String s;
  serializeJson(d, s);
  publicar(temaCanal(c.id, "state"), s, true);
}

void mqttPublicarEvento(const Canal& c, const char* evt, const Alarma& a, float valor) {
  JsonDocument d;
  d["evt"] = evt;
  d["nombre"] = a.nombre;
  d["cond"] = a.cond == Cond::Mayor ? "mayor" : a.cond == Cond::Menor ? "menor" : a.cond == Cond::Activo ? "activo" : "inactivo";
  d["valor"] = valor;
  String s;
  serializeJson(d, s);
  publicar(temaCanal(c.id, "evt"), s);
}

// Confirmacion del comando. `rb` = lectura real del pin al accionar (-1: no aplica); `us` = microsegundos desde que
// llego el comando hasta que el pin quedo verificado. El servidor suma el viaje de ida y vuelta por la red.
static void ack(const char* canalId, const char* idOrden, bool ok, const String& detalle, int rb = -1, uint32_t us = 0) {
  JsonDocument d;
  d["id"] = idOrden;
  d["ok"] = ok;
  if (detalle.length()) d["detail"] = detalle;
  if (rb >= 0) { d["rb"] = rb == 1; d["us"] = us; }
  String s;
  serializeJson(d, s);
  publicar(temaCanal(canalId, "ack"), s);
}

// Segunda confirmacion de un pulso: ya termino y el pin volvio a su nivel de reposo.
static void alFinDePulso(const Canal& c, bool pinEnReposo) {
  JsonDocument d;
  d["id"] = c.ordenId;
  d["fase"] = "fin";
  d["rb"] = pinEnReposo;
  String s;
  serializeJson(d, s);
  publicar(temaCanal(c.id, "ack"), s);
}

// Un comando que llega tarde NO se ejecuta (un porton no puede abrirse "despues").
static void alRecibir(char* topic, byte* payload, unsigned int len) {
  uint32_t t0 = micros();
  String t(topic);
  String base = String(cfg.base) + "/";
  Serial.printf("[mqtt] recibido %s (%u bytes)\n", topic, len);
  if (!t.startsWith(base)) return;
  String resto = t.substring(base.length());

  JsonDocument d;
  if (deserializeJson(d, (const char*)payload, len)) return;

  if (resto == "cfg") {
    const char* v = d["cfgv"] | "";
    if (v[0] && strcmp(v, cfg.cfgv) != 0) cfgNueva = true;
    return;
  }
  if (resto.startsWith("ch/") && resto.endsWith("/cmd")) {
    String id = resto.substring(3, resto.length() - 4);
    const char* idOrden = d["id"] | "";
    const char* valor = d["v"] | "";
    uint64_t exp = d["exp"] | 0ULL;
    if (horaOk() && exp && ahoraMs() > exp) { ack(id.c_str(), idOrden, false, "expirado"); return; }
    String detalle;
    bool rb = false;
    bool ok = canalesComando(id.c_str(), valor, detalle, &rb, idOrden);
    // Un comando "ok" cuyo pin no tomo el nivel pedido se informa como fallo de hardware.
    if (ok && !rb) { ok = false; detalle = "el pin no tomo el nivel esperado"; }
    ack(id.c_str(), idOrden, ok, detalle, ok || detalle.startsWith("el pin") ? (rb ? 1 : 0) : -1, micros() - t0);
  }
}

void mqttIniciar() {
  if (preparado) return;
  canalesFinPulsoCb(alFinDePulso);
  if (cfg.mqttTls) {
    seguro.setCACert(ISRG_ROOT_X1);
    mqtt.setClient(seguro);
  } else {
    mqtt.setClient(plano);
  }
  mqtt.setServer(cfg.mqttHost, cfg.mqttPort);
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(30);
  mqtt.setCallback(alRecibir);
  preparado = true;
}

bool mqttConectado() { return preparado && mqtt.connected(); }
void mqttDesconectar() { if (preparado && mqtt.connected()) mqtt.disconnect(); }
void mqttReiniciar() { if (preparado) { mqtt.disconnect(); preparado = false; primeraConexion = true; espera = 5000; } }
bool mqttCfgNueva() { bool v = cfgNueva; cfgNueva = false; return v; }

static bool conectar() {
  String clientId = String("ays-") + deviceId;
  String lwt = "{\"online\":false}";
  String temaStatus = tema("status");
  Serial.printf("[mqtt] conectando a %s:%u (%s)...\n", cfg.mqttHost, cfg.mqttPort, cfg.mqttTls ? "TLS" : "sin TLS");
  bool ok = mqtt.connect(clientId.c_str(), cfg.mqttUser[0] ? cfg.mqttUser : nullptr, cfg.mqttUser[0] ? cfg.mqttPass : nullptr,
                         temaStatus.c_str(), 0, true, lwt.c_str());
  if (!ok) {
    // Codigos de PubSubClient: -4 tiempo agotado, -3 conexion perdida, -2 no se pudo conectar, 4/5 usuario o clave no aceptados.
    Serial.printf("[mqtt] no se pudo conectar (estado %d)\n", mqtt.state());
    return false;
  }
  IPAddress ipServidor;
  WiFi.hostByName(cfg.mqttHost, ipServidor);
  Serial.printf("[mqtt] conectado (%s resuelve a %s; DNS %s)\n", cfg.mqttHost, ipServidor.toString().c_str(), WiFi.dnsIP().toString().c_str());
  mqtt.subscribe(tema("cfg").c_str());
  mqtt.subscribe((String(cfg.base) + "/ch/+/cmd").c_str());
  publicarStatus();
  canalesPublicarTodo();
  publicarMetricas();
  return true;
}

void mqttLoop() {
  if (!preparado || !netConectado()) return;
  if (!mqtt.connected()) {
    if (millis() - ultimoIntento < espera) return;
    ultimoIntento = millis();
    if (conectar()) {
      if (!primeraConexion) mqttReconexiones++;
      primeraConexion = false;
      espera = 5000;
    } else {
      espera = espera < 60000 ? espera * 2 : 60000;   // espera creciente hasta 1 min
    }
    return;
  }
  mqtt.loop();
  if (millis() - ultimoStatus >= STATUS_EVERY_MS) publicarStatus();
  if (millis() - ultimaMetrica >= (uint32_t)cfg.metricasS * 1000UL) publicarMetricas();
}
