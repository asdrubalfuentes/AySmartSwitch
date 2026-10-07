#include "net.h"
#include "config.h"
#include "store.h"
#include <WiFi.h>
#include <time.h>
#include <esp_task_wdt.h>

uint32_t wifiReconexiones = 0;
static bool yaConectoAlgunaVez = false;
static uint32_t ultimoIntento = 0;

static void onWifiEvent(WiFiEvent_t ev, WiFiEventInfo_t info) {
  if (ev == ARDUINO_EVENT_WIFI_STA_DISCONNECTED && yaConectoAlgunaVez) wifiReconexiones++;
  if (ev == ARDUINO_EVENT_WIFI_STA_GOT_IP) yaConectoAlgunaVez = true;
  // Registro de lo que pasa con el portal y el WiFi: sirve para saber por que se corta una conexion.
  if (ev == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
    const uint8_t* m = info.wifi_ap_staconnected.mac;
    Serial.printf("[wifi] celular conectado al portal %02x:%02x:%02x:%02x:%02x:%02x\n", m[0], m[1], m[2], m[3], m[4], m[5]);
  }
  if (ev == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
    const uint8_t* m = info.wifi_ap_stadisconnected.mac;
    Serial.printf("[wifi] celular desconectado del portal %02x:%02x:%02x:%02x:%02x:%02x\n", m[0], m[1], m[2], m[3], m[4], m[5]);
  }
  if (ev == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.printf("[wifi] desconectado de la red (motivo %d)\n", info.wifi_sta_disconnected.reason);
  }
  if (ev == ARDUINO_EVENT_WIFI_STA_GOT_IP) Serial.println("[wifi] conectado y con IP");
}

// El ESP32-C3 SuperMini trae una antena de PCB muy pequena y regulador justo: a potencia maxima el WiFi se
// vuelve inestable (la red desaparece o no logra conectar). Bajar la potencia es la correccion conocida.
void netAjustarRadio() {
  WiFi.setSleep(false);   // sin ahorro de energia: la red del portal no "duerme"
#if defined(AYS_TARGET_ESP32C3)
  WiFi.setTxPower(WIFI_POWER_11dBm);
#endif
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
  netAjustarRadio();
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
