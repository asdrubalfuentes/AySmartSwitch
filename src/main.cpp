#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "store.h"
#include "net.h"
#include "portal.h"
#include "servidor.h"
#include "canales.h"
#include "enlace_mqtt.h"
#include "actualizacion.h"

// aySmartSwitch v2 (ESP32-WROOM-32). Ciclo de vida:
//   1. Sin WiFi guardado -> portal cautivo (red AySmartSwitch-XXXX) para elegir WiFi y escribir el
//      codigo de adhesion que genera el administrador en la app.
//   2. Con WiFi y sin adherir -> canjea el codigo por HTTPS y recibe credenciales MQTT y canales.
//   3. Adherido -> pide su configuracion, se conecta por MQTT, publica estado/metricas y atiende
//      comandos; busca firmware nuevo en GitHub Releases cada 6 h (verificado con firma).

static bool ntpIniciado = false;
static bool configAlDia = false;
static uint32_t ultimoEnrolar = 0;
static uint32_t ultimoRefresco = 0;
static uint32_t ultimaOta = 0;
static bool otaInicialHecha = false;
static uint32_t inicioSinWifi = 0;
static uint32_t btnDesde = 0;

static void aplicarCanales() {
  canalesCargarJson(storeLoadCanales());
  canalesIniciar(mqttPublicarEstado, mqttPublicarEvento);
}

// ---- Boton BOOT: 3 s = portal, 10 s = restaurar de fabrica ----
static void botonLoop() {
  bool pulsado = digitalRead(BTN_PIN) == LOW;
  if (pulsado && !btnDesde) btnDesde = millis();
  if (!pulsado && btnDesde) {
    uint32_t t = millis() - btnDesde;
    btnDesde = 0;
    if (t >= 10000) storeFactoryReset();
    else if (t >= 3000 && !portalActivo()) portalIniciar(true);
  }
}

// ---- LED: portal = parpadeo rapido; sin WiFi = lento; todo en orden = apagado ----
static void ledLoop() {
  uint32_t periodo = 0;
  if (portalActivo()) periodo = 250;
  else if (!netConectado() && cfg.ssid[0]) periodo = 1000;
  else if (cfg.enrolled && !mqttConectado()) periodo = 2000;
  digitalWrite(LED_PIN, periodo ? ((millis() / periodo) & 1) : LOW);
}

static void adherir() {
  if (cfg.enrolled || !cfg.code[0] || !netConectado()) return;
  if (ultimoEnrolar && millis() - ultimoEnrolar < 20000) return;
  ultimoEnrolar = millis();

  String error;
  Serial.println("[adhesion] canjeando codigo...");
  switch (servidorEnrolar(error)) {
    case RespuestaServidor::Ok:
      Serial.println("[adhesion] OK");
      portalMensaje("");
      mqttReiniciar();
      aplicarCanales();
      configAlDia = true;
      ultimoRefresco = millis();
      break;
    case RespuestaServidor::SinRed:
      Serial.printf("[adhesion] sin respuesta del servidor (%s); se reintenta\n", error.c_str());
      break;
    default:
      // Codigo vencido o ya usado: se descarta y se pide uno nuevo en el portal.
      Serial.printf("[adhesion] rechazada: %s\n", error.c_str());
      storeSaveCode("");
      portalMensaje("No se pudo adherir: " + error + " Pide un codigo nuevo.");
      break;
  }
}

static void refrescarConfig() {
  if (!cfg.enrolled || !netConectado()) return;
  if (configAlDia && (millis() - ultimoRefresco) < 6UL * 3600UL * 1000UL) return;
  if (ultimoRefresco && !configAlDia && (millis() - ultimoRefresco) < 30000) return;
  ultimoRefresco = millis();

  String antes = cfg.cfgv;
  String error;
  switch (servidorRefrescarConfig(error)) {
    case RespuestaServidor::Ok:
      configAlDia = true;
      if (antes != cfg.cfgv) {
        Serial.printf("[cfg] configuracion nueva (%s)\n", cfg.cfgv);
        aplicarCanales();
        canalesPublicarTodo();
      }
      break;
    case RespuestaServidor::Desvinculado:
      // El administrador desvinculo el equipo: vuelve al estado de fabrica de adhesion.
      Serial.println("[cfg] desvinculado por el servidor");
      mqttDesconectar();
      cfg.enrolled = false;
      cfg.mqttPass[0] = 0;
      cfg.base[0] = 0;
      cfg.cfgv[0] = 0;
      storeSaveEnroll();
      storeSaveCanales("[]");
      mqttReiniciar();
      aplicarCanales();
      portalMensaje("Este equipo fue desvinculado. Escribe un codigo de adhesion nuevo.");
      break;
    default:
      Serial.printf("[cfg] no se pudo actualizar: %s\n", error.c_str());
      break;
  }
}

static void ota(bool forzada) {
  if (!netConectado()) return;
  if (!forzada) {
    if (!cfg.enrolled) return;
    if (!otaInicialHecha && millis() < 60000) return;     // deja estabilizar la conexion
    if (otaInicialHecha && (millis() - ultimaOta) < OTA_CHECK_EVERY_MS) return;
  }
  otaInicialHecha = true;
  ultimaOta = millis();
  if (!horaOk()) return;
  ResultadoOta r = actualizarSiHayNueva(cfg.otaOwner, cfg.otaRepo, FW_VERSION);
  if (!r.ok) Serial.printf("[ota] no se pudo comprobar: %s\n", r.error);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);
  storeInit();
  Serial.printf("\n[aySmartSwitch] fw %s  equipo %s  %s\n", FW_VERSION, deviceId, cfg.enrolled ? "adherido" : "sin adherir");

  esp_task_wdt_init(60, true);
  esp_task_wdt_add(NULL);

  // Lo primero: los pines en estado seguro (un rele nunca queda activo por un reinicio).
  aplicarCanales();

  netIniciar();
  if (cfg.ssid[0]) {
    Serial.printf("[wifi] conectando a %s...\n", cfg.ssid);
    if (netConectar(WIFI_TIMEOUT_MS)) Serial.printf("[wifi] conectado, IP %s\n", WiFi.localIP().toString().c_str());
  }
  inicioSinWifi = millis();
  if (cfg.enrolled) mqttIniciar();
}

void loop() {
  esp_task_wdt_reset();
  botonLoop();
  canalesLoop();
  ledLoop();
  netLoop();

  bool conectado = netConectado();
  if (conectado) {
    inicioSinWifi = millis();
    if (!ntpIniciado) { netIniciarHora(); ntpIniciado = true; }
    adherir();
    if (cfg.enrolled) {
      mqttIniciar();
      refrescarConfig();
      mqttLoop();
      if (mqttCfgNueva()) configAlDia = false;
    }
    if (portalPidioActualizar()) ota(true); else ota(false);
  } else if (cfg.ssid[0] && (millis() - inicioSinWifi) > WIFI_FAIL_TO_PORTAL_MS && !portalActivo()) {
    Serial.println("[wifi] sin conexion por 5 min: se abre el portal para reconfigurar");
    portalIniciar(true);
  }

  // El portal hace falta cuando no hay WiFi guardado o cuando, ya con WiFi, falta el codigo de adhesion.
  bool necesitaPortal = !cfg.ssid[0] || (conectado && !cfg.enrolled && !cfg.code[0]);
  if (necesitaPortal && !portalActivo()) portalIniciar(false);
  portalLoop();
}
