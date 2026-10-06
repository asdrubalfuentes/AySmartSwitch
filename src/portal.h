#pragma once
#include <Arduino.h>

// Portal cautivo: el equipo abre su propia red WiFi (AySmartSwitch-XXXX, clave = ultimos 8
// digitos de su MAC, impresa en la etiqueta) y, al conectarse desde el celular, se abre una
// pagina para elegir la red WiFi del sitio y escribir el codigo de adhesion.

void portalIniciar(bool porFallaWifi);
void portalDetener();
void portalLoop();
bool portalActivo();
bool portalPidioActualizar();        // true una vez si el usuario toco "Buscar actualizacion"
void portalMensaje(const String& texto);   // aviso que se muestra en la pagina (ej. codigo rechazado)
String portalSsid();
String portalClave();
