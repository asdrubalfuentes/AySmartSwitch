#pragma once
#include <Arduino.h>

// Consola de diagnostico por el puerto serie (115200). Pensada para instalar y probar el
// equipo con un cable USB: ver su estado, probar un rele o una entrada y cargar el WiFi/codigo
// sin celular. Escribir "ayuda" para ver los comandos. Requiere acceso fisico al equipo.
void diagLoop();
