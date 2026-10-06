#pragma once
#include <Arduino.h>

enum class RespuestaServidor : uint8_t { Ok, Rechazado, SinRed, Desvinculado };

// Canjea el codigo de adhesion (cfg.code) y guarda credenciales MQTT y canales.
RespuestaServidor servidorEnrolar(String& error);
// Pide la configuracion vigente. Desvinculado = el servidor ya no reconoce al equipo.
RespuestaServidor servidorRefrescarConfig(String& error);
