#pragma once
#include <Arduino.h>

// ---- Version: el CI la inyecta desde el tag vX.Y.Z (FW_VERSION_OVERRIDE) ----
#ifdef FW_VERSION_OVERRIDE
#define FW_VERSION FW_VERSION_OVERRIDE
#else
#define FW_VERSION "2.0.0"
#endif

// ---- Placa (la define platformio.ini) ----
#if defined(AYS_TARGET_ESP32C3)
  #define MODEL_NAME "ESP32-C3-SuperMini"
  #define FW_TARGET "esp32c3"        // nombre de los archivos del release: firmware-esp32c3.bin ...
  #define BTN_PIN 9                  // boton BOOT (GPIO9): 3 s = portal, 10 s = restaurar de fabrica
  #define LED_PIN 8                  // LED azul de la placa (GPIO8)
  #define LED_ACTIVO_BAJO 1          // en el SuperMini el LED enciende con nivel BAJO
#else
  #define MODEL_NAME "ESP32-WROOM-32"
  #define FW_TARGET "esp32"
  #define BTN_PIN 0                  // boton BOOT (GPIO0)
  #define LED_PIN 2                  // LED de la placa
  #define LED_ACTIVO_BAJO 0
#endif

// Servidor de AYSAFI (monitor-api): adhesion y configuracion por HTTPS.
#define SERVER_BASE "https://emqx.aysafi.com:8450/api"

// Firmware: de donde se descarga (GitHub Releases). El servidor puede indicar otro repo al adherirse.
#define OTA_OWNER_DEFAULT "asdrubalfuentes"
#define OTA_REPO_DEFAULT "AySmartSwitch"
#define OTA_CHECK_EVERY_MS (6UL * 3600UL * 1000UL)

// ---- Capacidad ----
#define MAX_CANALES 8
#define MAX_ALARMAS 4

// ---- Tiempos (ms) ----
#define WIFI_TIMEOUT_MS 20000UL
#define WIFI_FAIL_TO_PORTAL_MS 300000UL   // 5 min sin lograr WiFi -> se abre el portal
#define PORTAL_TIMEOUT_MS 600000UL        // el portal por falla de WiFi se cierra a los 10 min
#define STATUS_EVERY_MS 60000UL
#define DHT_EVERY_MS 10000UL
#define ADC_EVERY_MS 2000UL
#define STATE_REFRESH_MS 60000UL          // reenviar el estado aunque no cambie
#define CICLOS_SAVE_EVERY_MS 600000UL

// Un id de equipo son los 12 digitos hex de la MAC en minuscula (ej. a1b2c3d4e5f6).
// Con el mismo formato que usa el servidor y el firmware anterior (6 hex) como sufijo.
