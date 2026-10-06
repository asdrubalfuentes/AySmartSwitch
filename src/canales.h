#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

// Un "canal" es una funcion del instrumento conectada a un pin: rele, entrada digital,
// temperatura/humedad (DHT) o lectura analogica. El servidor los define y los envia al
// adherir el equipo; el id del canal es el id de la funcion en el servidor.
enum class Kind : uint8_t { None, Rele, Entrada, DhtTemp, DhtHum, Adc };
enum class Cond : uint8_t { Mayor, Menor, Activo, Inactivo };

struct Alarma {
  Cond cond;
  float valor;
  float hist;
  char nombre[24];
  bool activa;
};

struct Canal {
  char id[40];
  Kind kind;
  uint8_t pin;
  uint16_t pulsoMs;     // rele: 0 = solo on/off
  bool invertido;       // rele: modulo activo en bajo / entrada: logica invertida
  bool pullup;
  uint16_t debounceMs;
  bool dht22;
  float amin, amax;     // adc: rango de ingenieria
  char unidad[8];
  Alarma alarmas[MAX_ALARMAS];
  uint8_t nAlarmas;
  // ---- estado en ejecucion ----
  bool activo;          // rele energizado / entrada activa
  void* timer;          // rele: temporizador de hardware que corta el pulso aunque el ciclo principal este ocupado
  volatile bool finPulso;  // el temporizador ya solto el rele (falta avisarlo)
  bool inRaw;
  uint32_t inCambio;
  bool inInit;
  float valor;
  bool tieneValor;
  uint32_t ultimaPub;
  float ultimoPub;
  uint32_t ciclos;      // activaciones acumuladas (desgaste)
};

typedef void (*EstadoCb)(const Canal& c, const char* valor);
typedef void (*EventoCb)(const Canal& c, const char* evt, const Alarma& a, float valor);

void canalesCargarJson(const String& json);               // reemplaza la lista (configuracion del servidor)
void canalesIniciar(EstadoCb est, EventoCb ev);           // pines en estado seguro y listo para leer
void canalesLoop();
bool canalesComando(const char* id, const char* valor, String& detalle);
void canalesPublicarTodo();                               // reenvia el estado actual (al conectar MQTT)
int canalesCount();
void canalesCiclosJson(JsonObject out);
void canalesGuardarCiclos(bool forzar);
