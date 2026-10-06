#pragma once
#include <Arduino.h>

// Actualizacion por GitHub Releases (mismo modelo que nodeIO / nodeIO_master):
//   https://github.com/<owner>/<repo>/releases/latest/download/{version.txt, firmware.sha256, firmware.sig, firmware.bin}
// Ademas del SHA-256, cada firmware viene FIRMADO (ECDSA P-256) y el equipo lo verifica con la
// clave publica incrustada: aunque alguien interceptara la descarga, no podria instalar nada propio.

enum class Fase : uint8_t { Consulta, AlDia, Descarga, Verificacion, Listo, Error };

struct ResultadoOta {
  bool ok = false;            // sin errores (aunque no hubiera actualizacion)
  bool hayNueva = false;
  char ultima[24] = {0};
  char error[72] = {0};
};

// true si latest > current (comparacion numerica X.Y.Z, tolera "v" y sufijos "-xyz").
bool versionMasNueva(const char* latest, const char* current);

// Consulta version.txt y, si hay una mayor, la descarga, verifica y reinicia (no retorna si instala).
ResultadoOta actualizarSiHayNueva(const char* owner, const char* repo, const char* versionActual);
