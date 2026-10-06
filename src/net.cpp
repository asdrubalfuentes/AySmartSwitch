#include "net.h"
#include "config.h"
#include "store.h"
#include <WiFi.h>
#include <time.h>
#include <esp_task_wdt.h>

uint32_t wifiReconexiones = 0;
static bool yaConectoAlgunaVez = false;
static uint32_t ultimoIntento = 0;

static void onWifiEvent(WiFiEvent_t ev) {
  if (ev == ARDUINO_EVENT_WIFI_STA_DISCONNECTED && yaConectoAlgunaVez) wifiReconexiones++;
  if (ev == ARDUINO_EVENT_WIFI_STA_GOT_IP) yaConectoAlgunaVez = true;
}

void netIniciar() {
  WiFi.persistent(false);       // las credenciales las guardamos nosotros
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(onWifiEvent);
}

bool netConectado() { return WiFi.status() == WL_CONNECTED; }

bool netConectar(uint32_t timeoutMs) {
  if (!cfg.ssid[0]) return false;
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid, cfg.pass);
  uint32_t ini = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - ini) < timeoutMs) {
    delay(250);
    esp_task_wdt_reset();
  }
  return WiFi.status() == WL_CONNECTED;
}

void netLoop() {
  if (netConectado() || !cfg.ssid[0]) return;
  if (millis() - ultimoIntento < 15000) return;
  ultimoIntento = millis();
  WiFi.disconnect();
  WiFi.begin(cfg.ssid, cfg.pass);
}

void netIniciarHora() { configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com"); }

bool horaOk() { return time(nullptr) > 1700000000; }

uint64_t ahoraMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

void netSincronizarHora(uint32_t timeoutMs) {
  if (horaOk()) return;
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  uint32_t ini = millis();
  while (!horaOk() && (millis() - ini) < timeoutMs) {
    delay(200);
    esp_task_wdt_reset();
  }
}
