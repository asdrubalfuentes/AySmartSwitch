#pragma once
#include <Arduino.h>
#include "canales.h"

extern uint32_t mqttReconexiones;

void mqttIniciar();                       // prepara el cliente segun cfg (host, puerto, TLS, credenciales)
void mqttLoop();                          // reconecta, procesa mensajes y publica estado/metricas periodicas
bool mqttConectado();
void mqttReiniciar();                     // vuelve a leer host/credenciales (tras adherirse de nuevo)
void mqttDesconectar();                   // antes de actualizar el firmware o reiniciar
bool mqttCfgNueva();                      // true una vez cuando el servidor avisa que hay configuracion nueva

// Callbacks que usa canales.cpp para informar cambios.
void mqttPublicarEstado(const Canal& c, const char* valor);
void mqttPublicarEvento(const Canal& c, const char* evt, const Alarma& a, float valor);
