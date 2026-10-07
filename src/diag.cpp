#include "diag.h"
#include "config.h"
#include "store.h"
#include "net.h"
#include "portal.h"
#include "canales.h"
#include "enlace_mqtt.h"
#include <WiFi.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <driver/gpio.h>

namespace {

// Pines que se pueden manejar a mano con "salida": los mismos que la app permite asignar a un rele.
#if defined(AYS_TARGET_ESP32C3)
const uint8_t PINES_PRUEBA[] = {0, 1, 3, 4, 5, 6, 7, 10};
#else
const uint8_t PINES_PRUEBA[] = {4, 5, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
#endif

String linea;

bool pinDePrueba(int pin) {
  for (uint8_t p : PINES_PRUEBA) if (p == pin) return true;
  return false;
}

// Separa la primera palabra del resto.
String palabra(String& resto) {
  resto.trim();
  int i = resto.indexOf(' ');
  String w = i < 0 ? resto : resto.substring(0, i);
  resto = i < 0 ? String() : resto.substring(i + 1);
  resto.trim();
  return w;
}

void ayuda() {
  Serial.println(
    "\nComandos:\n"
    "  estado                 resumen del equipo, WiFi y servidor\n"
    "  canales                canales (reles, entradas, sensores) con su estado\n"
    "  rele <n> pulso|on|off  acciona el canal <n> de la lista (no hace falta estar en linea)\n"
    "  pin <n>                lee el nivel de un pin (0/1)\n"
    "  escuchar <pin> [seg]   vigila una entrada (con pull-up) y muestra cada cambio: sirve para probar un sensor\n"
    "  led                    parpadea 3 veces el LED de la placa (prueba visual)\n"
    "  salida <pin> [ms]      pulso de prueba en un pin permitido (por defecto 500 ms, maximo 5000)\n"
    "  wifi                   busca redes WiFi\n"
    "  wifi <red>|<clave>     guarda el WiFi y reinicia\n"
    "  codigo ABCD-EFGH       guarda el codigo de adhesion (se canjea al conectar)\n"
    "  portal                 abre el portal cautivo\n"
    "  reiniciar              reinicia el equipo\n"
    "  fabrica confirmar      borra TODO (WiFi, adhesion, canales) y reinicia\n");
}

void estado() {
  Serial.printf("\n[estado] fw %s  %s  id %s\n", FW_VERSION, MODEL_NAME, deviceId);
  Serial.printf("  adherido: %s   cfgv: %s\n", cfg.enrolled ? "si" : "no", cfg.cfgv[0] ? cfg.cfgv : "-");
  Serial.printf("  WiFi guardado: %s   conectado: %s", cfg.ssid[0] ? cfg.ssid : "(ninguno)", netConectado() ? "si" : "no");
  if (netConectado()) Serial.printf("   IP %s  RSSI %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  Serial.println();
  Serial.printf("  MQTT: %s   base: %s\n", mqttConectado() ? "conectado" : "sin conexion", cfg.base[0] ? cfg.base : "-");
  Serial.printf("  codigo de adhesion pendiente: %s\n", cfg.code[0] ? cfg.code : "no");
  Serial.printf("  portal: %s   hora NTP: %s\n", portalActivo() ? "abierto" : "cerrado", horaOk() ? "ok" : "sin hora");
  Serial.printf("  memoria libre: %u bytes   minima: %u   en marcha: %lu s\n", ESP.getFreeHeap(), ESP.getMinFreeHeap(), millis() / 1000UL);
  Serial.printf("  reconexiones WiFi: %lu   MQTT: %lu\n", (unsigned long)wifiReconexiones, (unsigned long)mqttReconexiones);
  Serial.printf("  canales: %d\n", canalesCount());
}

void buscarWifi() {
  Serial.println("[wifi] buscando redes...");
  int n = WiFi.scanNetworks();
  if (n <= 0) { Serial.println("  (ninguna red encontrada)"); return; }
  for (int i = 0; i < n; i++) Serial.printf("  %2d  %-32s  %4d dBm  %s\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "abierta" : "protegida");
  WiFi.scanDelete();
}

void rele(String resto) {
  String a = palabra(resto);
  String v = palabra(resto);
  if (!a.length() || !v.length()) { Serial.println("uso: rele <n> pulso|on|off"); return; }
  char id[40];
  if (!canalesIdPorIndice(a.toInt(), id, sizeof(id))) { Serial.println("no existe ese canal (mira la lista con 'canales')"); return; }
  String detalle;
  bool rb = false;
  bool ok = canalesComando(id, v.c_str(), detalle, &rb, "diag");
  Serial.printf("[rele %d] %s %s%s%s  | lectura del pin: %s\n", (int)a.toInt(), v.c_str(), ok ? "OK" : "RECHAZADO", detalle.length() ? ": " : "", detalle.c_str(), rb ? "confirmada" : "NO coincide");
}

void salida(String resto) {
  String a = palabra(resto);
  String m = palabra(resto);
  int pin = a.toInt();
  if (!a.length() || !pinDePrueba(pin)) { Serial.println("pin no permitido para pruebas (son los mismos pines que la app deja asignar a un rele)"); return; }
  uint32_t ms = m.length() ? (uint32_t)m.toInt() : 500;
  if (ms < 20) ms = 20;
  if (ms > 5000) ms = 5000;
  pinMode(pin, OUTPUT);
  gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT_OUTPUT);   // con lectura habilitada
  digitalWrite(pin, HIGH);
  int leidoAlto = digitalRead(pin);
  Serial.printf("[salida] pin %d en ALTO por %lu ms... lectura real del pin: %d %s\n", pin, (unsigned long)ms, leidoAlto, leidoAlto == 1 ? "(confirmado)" : "(NO coincide: pin en corto o dañado)");
  delay(ms);
  digitalWrite(pin, LOW);
  int leidoBajo = digitalRead(pin);
  Serial.printf("[salida] pin %d en BAJO (listo). lectura real: %d %s\n", pin, leidoBajo, leidoBajo == 0 ? "(confirmado)" : "(NO coincide)");
}

void leerPin(String resto) {
  String a = palabra(resto);
  if (!a.length()) { Serial.println("uso: pin <n>"); return; }
  int pin = a.toInt();
  Serial.printf("[pin %d] nivel %d\n", pin, digitalRead(pin));
}

// Vigila una entrada unos segundos e informa cada cambio (probar un contacto magnetico, un pulsador...).
void escuchar(String resto) {
  String a = palabra(resto);
  String s = palabra(resto);
  int pin = a.toInt();
  if (!a.length() || !pinDePrueba(pin)) { Serial.println("pin no permitido (usa uno de los que la app deja asignar a una entrada)"); return; }
  uint32_t seg = s.length() ? (uint32_t)s.toInt() : 15;
  if (seg < 1) seg = 1;
  if (seg > 120) seg = 120;
  pinMode(pin, INPUT_PULLUP);
  int previo = digitalRead(pin);
  Serial.printf("[escuchar] pin %d con pull-up por %lu s. Nivel inicial: %d (%s)\n", pin, (unsigned long)seg, previo, previo ? "abierto / sin conectar" : "a GND");
  uint32_t fin = millis() + seg * 1000UL;
  uint32_t cambios = 0;
  while ((int32_t)(fin - millis()) > 0) {
    int v = digitalRead(pin);
    if (v != previo) {
      delay(20);                                   // antirrebote
      if (digitalRead(pin) == v) { previo = v; cambios++; Serial.printf("  [%lu ms] pin %d -> %d (%s)\n", (unsigned long)millis(), pin, v, v ? "se abrio" : "se cerro a GND"); }
    }
    esp_task_wdt_reset();
    delay(2);
  }
  Serial.printf("[escuchar] fin: %lu cambios\n", (unsigned long)cambios);
}

void parpadear() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, LED_ACTIVO_BAJO ? LOW : HIGH);
    delay(250);
    digitalWrite(LED_PIN, LED_ACTIVO_BAJO ? HIGH : LOW);
    delay(250);
  }
  Serial.println("[led] listo");
}

void procesar(String cmd) {
  cmd.trim();
  if (!cmd.length()) return;
  String resto = cmd;
  String c = palabra(resto);
  c.toLowerCase();
  if (c == "ayuda" || c == "help" || c == "?") ayuda();
  else if (c == "estado") estado();
  else if (c == "canales") { Serial.println("[canales]"); canalesDescribir(Serial); }
  else if (c == "rele") rele(resto);
  else if (c == "pin") leerPin(resto);
  else if (c == "salida") salida(resto);
  else if (c == "escuchar") escuchar(resto);
  else if (c == "led") parpadear();
  else if (c == "wifi") {
    if (!resto.length()) { buscarWifi(); return; }
    int i = resto.indexOf('|');
    String red = i < 0 ? resto : resto.substring(0, i);
    String clave = i < 0 ? String() : resto.substring(i + 1);
    red.trim();
    if (!red.length() || red.length() > 32 || clave.length() > 64) { Serial.println("uso: wifi <red>|<clave>"); return; }
    storeSaveWifi(red.c_str(), clave.c_str());
    Serial.printf("[wifi] guardado '%s'. Reiniciando...\n", red.c_str());
    delay(300);
    ESP.restart();
  } else if (c == "codigo") {
    String codigo;
    for (size_t i = 0; i < resto.length(); i++) if (isalnum((unsigned char)resto[i])) codigo += (char)toupper(resto[i]);
    if (codigo.length() != 8) { Serial.println("el codigo debe tener 8 caracteres (ABCD-EFGH)"); return; }
    storeSaveCode(codigo.c_str());
    Serial.println("[codigo] guardado: se canjea apenas haya WiFi");
  } else if (c == "portal") {
    portalIniciar(false);
    Serial.println("[portal] abierto");
  } else if (c == "reiniciar") {
    Serial.println("Reiniciando...");
    delay(200);
    ESP.restart();
  } else if (c == "fabrica") {
    if (resto == "confirmar") { Serial.println("Restaurando de fabrica..."); delay(200); storeFactoryReset(); }
    else Serial.println("Esto borra WiFi, adhesion y canales. Escribe: fabrica confirmar");
  } else {
    Serial.printf("comando desconocido: %s (escribe 'ayuda')\n", c.c_str());
  }
}

}  // namespace

void diagLoop() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r' || ch == '\n') {
      if (linea.length()) { String l = linea; linea = ""; procesar(l); }
    } else if (linea.length() < 120) {
      linea += ch;
    }
  }
}
