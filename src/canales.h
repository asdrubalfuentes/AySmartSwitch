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
  // ---- confirmacion por lectura del pin (checkback) ----
  char ordenId[12];     // id del ultimo comando recibido (para atar la confirmacion del fin del pulso)
  bool rbUlt;           // el pin tomo el nivel esperado al accionarlo
  volatile bool rbFinOk;   // al terminar el pulso, el pin volvio al reposo
};

typedef void (*EstadoCb)(const Canal& c, const char* valor);
typedef void (*FinPulsoCb)(const Canal& c, bool pinEnReposo);   // el pulso termino: informa si el pin volvio al reposo
void canalesFinPulsoCb(FinPulsoCb cb);
typedef void (*EventoCb)(const Canal& c, const char* evt, const Alarma& a, float valor);

void canalesCargarJson(const String& json);               // reemplaza la lista (configuracion del servidor)
void canalesIniciar(EstadoCb est, EventoCb ev);           // pines en estado seguro y listo para leer
void canalesLoop();
// rb (opcional) devuelve si, al accionar, la lectura del pin confirmo el nivel esperado.
bool canalesComando(const char* id, const char* valor, String& detalle, bool* rb = nullptr, const char* ordenId = nullptr);
void canalesPublicarTodo();                               // reenvia el estado actual (al conectar MQTT)
int canalesCount();
void canalesDescribir(Print& out);                        // lista de canales (diagnostico por serie)
bool canalesIdPorIndice(int i, char* out, size_t n);        // id del canal i (diagnostico)
// Estado del LED integrado respecto a los reles: 0 = nada que mostrar, 1 = rele accionado y sin confirmar
// (encendido), 2 = la lectura del pin no coincidio con lo pedido (falla de hardware).
uint8_t canalesLedModo();
void canalesCiclosJson(JsonObject out);
void canalesGuardarCiclos(bool forzar);
