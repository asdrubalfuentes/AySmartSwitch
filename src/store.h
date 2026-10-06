#pragma once
#include <Arduino.h>

// Todo lo que el equipo recuerda entre reinicios (NVS).
struct Cfg {
  char ssid[33];
  char pass[65];
  char code[16];        // codigo de adhesion pendiente (se borra al adherirse)
  bool enrolled;
  char mqttHost[64];
  uint16_t mqttPort;
  bool mqttTls;
  char mqttUser[20];
  char mqttPass[48];
  char base[80];        // aysafi/<org>/<deviceId>
  char otaOwner[32];
  char otaRepo[40];
  char cfgv[16];        // version de la configuracion de canales
  uint16_t metricasS;
};

extern Cfg cfg;
extern char deviceId[13];   // 12 hex de la MAC, minuscula

void storeInit();
void storeSaveWifi(const char* ssid, const char* pass);
void storeSaveCode(const char* code);
void storeSaveEnroll();                       // guarda lo que hay en cfg (adhesion)
void storeSaveCanales(const String& json);
String storeLoadCanales();
void storeSaveCiclos(const String& json);
String storeLoadCiclos();
void storeFactoryReset();                     // borra todo y reinicia
