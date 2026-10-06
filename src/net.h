#pragma once
#include <Arduino.h>

extern uint32_t wifiReconexiones;

void netIniciar();
bool netConectado();
// Intenta conectar al WiFi guardado. Bloquea hasta lograrlo o agotar el tiempo.
bool netConectar(uint32_t timeoutMs);
void netLoop();                       // reintenta si se cayo
void netSincronizarHora(uint32_t timeoutMs);
void netIniciarHora();                // pide la hora por NTP sin esperar
bool horaOk();
uint64_t ahoraMs();                   // epoch en ms (solo valido si horaOk())
