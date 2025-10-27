// Clean, minimal board definition compatible with ESP8266 (ESP-01S) and ESP32.
#ifndef BOARD_DEF_H
#define BOARD_DEF_H

#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <AsyncTCP.h>
#else
#error "Unsupported platform (ESP8266/ESP32 only)"
#endif

#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <MQTTPubSubClient.h>
#include <HTTPClient.h>
#include <Update.h>

// WiFi credentials (se pueden ajustar en tiempo de ejecución)
extern String ssid;
extern String pass;

// MQTT
extern const char* mqttServer;
extern const uint16_t mqttPort;
extern WiFiClient netClient;
extern MQTTPubSubClient mqtt;

// Identidad del dispositivo y tópicos
extern String idUnico;
extern String TOPICO_SUB;
extern String TOPICO_PUB;
extern String TOPICO_PUB_HEARTBIT;

// Pines por defecto
#ifndef RELAY_PIN
#  if defined(ESP8266)
#    define RELAY_PIN 0   // GPIO0 en ESP-01S
#  else
#    define RELAY_PIN 2   // por defecto en ESP32, se puede cambiar
#  endif
#endif

#define RELE RELAY_PIN

// Versión embebida desde el script de build
#ifndef FW_VERSION
#define FW_VERSION "00.00.local:0"
#endif
#ifndef FW_VERSION_SHORT
#define FW_VERSION_SHORT "00.00.0"
#endif

// OTA HTTP (pull) - URLs del servidor de actualización
extern const char* versionURL;
extern const char* firmwareURL;

// API pública
void initBoard();
void beginWiFi(const String& ssid_, const String& pass_);
void setupOTA();
void connect(); // WiFi + TCP + MQTT
void mqttUpdate();
void chequearActualizaciones();

// Utilidades
const char* getFwVersion();
const char* getFwVersionShort();
void ledOn();
void ledOff();

#endif // BOARD_DEF_H
