#include "canales.h"
#include "store.h"
#include <DHT.h>
#include <esp_timer.h>

static Canal canales[MAX_CANALES];
static int n = 0;
static EstadoCb cbEstado = nullptr;
static EventoCb cbEvento = nullptr;

// Un mismo sensor DHT entrega temperatura y humedad por el mismo pin.
struct DhtSlot {
  uint8_t pin;
  DHT* dht;
  uint32_t ultima;
  float t, h;
  bool ok;
};
static DhtSlot dhts[2];
static int nDhts = 0;

static uint32_t cicloSucio = 0;      // activaciones sin guardar
static uint32_t ultimoGuardado = 0;
static uint32_t ultimoAdc = 0;

static inline uint8_t nivelActivo(const Canal& c) { return c.invertido ? LOW : HIGH; }
static inline uint8_t nivelReposo(const Canal& c) { return c.invertido ? HIGH : LOW; }

static void fmt(char* out, size_t len, float v) { snprintf(out, len, "%.1f", v); }

static Kind kindDe(const char* s) {
  if (!strcmp(s, "rele")) return Kind::Rele;
  if (!strcmp(s, "entrada")) return Kind::Entrada;
  if (!strcmp(s, "dht_temp")) return Kind::DhtTemp;
  if (!strcmp(s, "dht_hum")) return Kind::DhtHum;
  if (!strcmp(s, "adc")) return Kind::Adc;
  return Kind::None;
}

static Cond condDe(const char* s) {
  if (!strcmp(s, "mayor")) return Cond::Mayor;
  if (!strcmp(s, "menor")) return Cond::Menor;
  if (!strcmp(s, "inactivo")) return Cond::Inactivo;
  return Cond::Activo;
}

static DhtSlot* slotDht(uint8_t pin, bool dht22) {
  for (int i = 0; i < nDhts; i++) if (dhts[i].pin == pin) return &dhts[i];
  if (nDhts >= 2) return nullptr;
  DhtSlot& s = dhts[nDhts++];
  s.pin = pin;
  s.dht = new DHT(pin, dht22 ? DHT22 : DHT11);
  s.dht->begin();
  s.ultima = 0;
  s.ok = false;
  s.t = s.h = 0;
  return &s;
}

// ---- Configuracion ----

void canalesCargarJson(const String& json) {
  // Libera los temporizadores de la configuracion anterior (un pulso en curso se corta).
  for (int i = 0; i < n; i++) {
    if (canales[i].timer) {
      esp_timer_stop((esp_timer_handle_t)canales[i].timer);
      esp_timer_delete((esp_timer_handle_t)canales[i].timer);
      canales[i].timer = nullptr;
    }
  }
  n = 0;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return;
  JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["canales"].as<JsonArray>();
  if (arr.isNull()) return;

  JsonDocument ciclos;
  deserializeJson(ciclos, storeLoadCiclos());

  for (JsonObject o : arr) {
    if (n >= MAX_CANALES) break;
    Canal& c = canales[n];
    memset(&c, 0, sizeof(c));
    strncpy(c.id, o["id"] | "", sizeof(c.id) - 1);
    c.kind = kindDe(o["kind"] | "");
    if (c.kind == Kind::None || !c.id[0]) continue;
    c.pin = o["pin"] | 0;
    c.pulsoMs = o["pulsoMs"] | 500;
    c.invertido = o["invertido"] | false;
    c.pullup = o["pullup"] | true;
    c.debounceMs = o["debounceMs"] | 50;
    c.dht22 = strcmp(o["modelo"] | "dht22", "dht11") != 0;
    c.amin = o["min"] | 0.0f;
    c.amax = o["max"] | 100.0f;
    strncpy(c.unidad, o["unidad"] | "", sizeof(c.unidad) - 1);
    for (JsonObject a : o["alarmas"].as<JsonArray>()) {
      if (c.nAlarmas >= MAX_ALARMAS) break;
      Alarma& al = c.alarmas[c.nAlarmas++];
      al.cond = condDe(a["condicion"] | "activo");
      al.valor = a["valor"] | 0.0f;
      al.hist = a["histeresis"] | 0.0f;
      strncpy(al.nombre, a["nombre"] | "Alarma", sizeof(al.nombre) - 1);
      al.activa = false;
    }
    c.ciclos = ciclos[c.id] | 0;
    n++;
  }
}

int canalesCount() { return n; }

static const char* nombreKind(Kind k) {
  switch (k) {
    case Kind::Rele: return "rele";
    case Kind::Entrada: return "entrada";
    case Kind::DhtTemp: return "dht_temp";
    case Kind::DhtHum: return "dht_hum";
    case Kind::Adc: return "adc";
    default: return "?";
  }
}

void canalesDescribir(Print& out) {
  if (!n) { out.println("  (sin canales: el equipo aun no esta adherido)"); return; }
  for (int i = 0; i < n; i++) {
    const Canal& c = canales[i];
    out.printf("  [%d] %-8s pin %-2u id %.8s..  ", i, nombreKind(c.kind), c.pin, c.id);
    if (c.kind == Kind::Rele) out.printf("%s  pulso %ums  %s  ciclos %lu\n", c.activo ? "ACTIVO" : "reposo", c.pulsoMs, c.invertido ? "activo-bajo" : "activo-alto", (unsigned long)c.ciclos);
    else if (c.kind == Kind::Entrada) out.printf("%s  nivel %d  pullup %d\n", c.activo ? "ACTIVA" : "inactiva", digitalRead(c.pin), c.pullup);
    else if (c.tieneValor) out.printf("valor %.1f %s\n", c.valor, c.unidad);
    else out.println("sin lectura");
  }
}

bool canalesIdPorIndice(int i, char* out, size_t len) {
  if (i < 0 || i >= n) return false;
  strncpy(out, canales[i].id, len - 1);
  out[len - 1] = 0;
  return true;
}

void canalesIniciar(EstadoCb est, EventoCb ev) {
  cbEstado = est;
  cbEvento = ev;
  for (int i = 0; i < n; i++) {
    Canal& c = canales[i];
    if (c.kind == Kind::Rele) {
      // Estado seguro primero: el rele nunca queda activo por un reinicio.
      digitalWrite(c.pin, nivelReposo(c));
      pinMode(c.pin, OUTPUT);
      digitalWrite(c.pin, nivelReposo(c));
      c.activo = false;
    } else if (c.kind == Kind::Entrada) {
      pinMode(c.pin, c.pullup ? INPUT_PULLUP : INPUT);
    } else if (c.kind == Kind::Adc) {
      pinMode(c.pin, INPUT);
      analogSetPinAttenuation(c.pin, ADC_11db);   // 0 a ~3,3 V
    } else if (c.kind == Kind::DhtTemp || c.kind == Kind::DhtHum) {
      slotDht(c.pin, c.dht22);
    }
  }
}

// ---- Alarmas ----

static void evaluarAlarmas(Canal& c, float v, bool digitalActivo) {
  for (uint8_t i = 0; i < c.nAlarmas; i++) {
    Alarma& a = c.alarmas[i];
    bool dispara = false, limpia = false;
    switch (a.cond) {
      case Cond::Mayor:    dispara = v > a.valor; limpia = v < a.valor - a.hist; break;
      case Cond::Menor:    dispara = v < a.valor; limpia = v > a.valor + a.hist; break;
      case Cond::Activo:   dispara = digitalActivo; limpia = !digitalActivo; break;
      case Cond::Inactivo: dispara = !digitalActivo; limpia = digitalActivo; break;
    }
    if (!a.activa && dispara) {
      a.activa = true;
      if (cbEvento) cbEvento(c, "alarma", a, v);
    } else if (a.activa && limpia) {
      a.activa = false;
      if (cbEvento) cbEvento(c, "normal", a, v);
    }
  }
}

// ---- Rele ----

// El pulso lo termina un temporizador de hardware, NO el ciclo principal: una conexion HTTPS o
// MQTT que se bloquee unos segundos no puede dejar un porton con el rele activo de mas.
static void finDePulso(void* arg) {
  Canal* c = (Canal*)arg;
  digitalWrite(c->pin, nivelReposo(*c));
  c->activo = false;
  c->finPulso = true;
}

static void detenerTemporizador(Canal& c) {
  if (c.timer) esp_timer_stop((esp_timer_handle_t)c.timer);
}

static void iniciarPulso(Canal& c) {
  if (!c.timer) {
    esp_timer_create_args_t args = {};
    args.callback = finDePulso;
    args.arg = &c;
    args.name = "pulso";
    esp_timer_create(&args, (esp_timer_handle_t*)&c.timer);
  }
  esp_timer_stop((esp_timer_handle_t)c.timer);
  esp_timer_start_once((esp_timer_handle_t)c.timer, (uint64_t)c.pulsoMs * 1000ULL);
}

static void releSet(Canal& c, bool on) {
  if (on == c.activo) return;
  digitalWrite(c.pin, on ? nivelActivo(c) : nivelReposo(c));
  c.activo = on;
  if (on) { c.ciclos++; cicloSucio++; }
  if (cbEstado) cbEstado(c, on ? "on" : "off");
}

bool canalesComando(const char* id, const char* valor, String& detalle) {
  for (int i = 0; i < n; i++) {
    Canal& c = canales[i];
    if (strcmp(c.id, id)) continue;
    if (c.kind != Kind::Rele) { detalle = "el canal no recibe comandos"; return false; }
    if (!strcmp(valor, "pulso")) {
      if (c.pulsoMs == 0) { detalle = "canal sin pulso configurado (usa on/off)"; return false; }
      detenerTemporizador(c);
      releSet(c, true);
      iniciarPulso(c);
      return true;
    }
    if (!strcmp(valor, "on"))  { detenerTemporizador(c); releSet(c, true); return true; }
    if (!strcmp(valor, "off")) { detenerTemporizador(c); releSet(c, false); return true; }
    detalle = "valor no soportado";
    return false;
  }
  detalle = "canal desconocido";
  return false;
}

// ---- Entradas ----

static bool leerEntrada(const Canal& c) {
  bool activa = c.pullup ? (digitalRead(c.pin) == LOW) : (digitalRead(c.pin) == HIGH);
  return c.invertido ? !activa : activa;
}

static void publicarDigital(Canal& c) {
  if (cbEstado) cbEstado(c, c.activo ? "activo" : "inactivo");
  c.ultimaPub = millis();
}

static void loopEntrada(Canal& c) {
  bool act = leerEntrada(c);
  if (!c.inInit) {
    c.inInit = true;
    c.inRaw = act;
    c.inCambio = millis();
    c.activo = act;
    publicarDigital(c);
    evaluarAlarmas(c, act ? 1.0f : 0.0f, act);
    return;
  }
  if (act != c.inRaw) { c.inRaw = act; c.inCambio = millis(); }
  else if (act != c.activo && (millis() - c.inCambio) >= c.debounceMs) {
    c.activo = act;
    publicarDigital(c);
    evaluarAlarmas(c, act ? 1.0f : 0.0f, act);
  }
  if ((millis() - c.ultimaPub) >= STATE_REFRESH_MS) publicarDigital(c);
}

// ---- Analogicas y DHT ----

static void publicarValor(Canal& c, float v, float umbralCambio) {
  bool cambio = !c.tieneValor || fabsf(v - c.ultimoPub) >= umbralCambio;
  bool refresco = (millis() - c.ultimaPub) >= STATE_REFRESH_MS;
  c.valor = v;
  c.tieneValor = true;
  if (cambio || refresco) {
    char buf[16];
    fmt(buf, sizeof(buf), v);
    if (cbEstado) cbEstado(c, buf);
    c.ultimoPub = v;
    c.ultimaPub = millis();
  }
  evaluarAlarmas(c, v, false);
}

static void loopAdc(Canal& c) {
  uint32_t suma = 0;
  for (int i = 0; i < 16; i++) suma += analogRead(c.pin);
  float crudo = suma / 16.0f;
  float v = c.amin + (crudo / 4095.0f) * (c.amax - c.amin);
  publicarValor(c, v, fabsf(c.amax - c.amin) * 0.005f);
}

static void loopDht() {
  for (int i = 0; i < nDhts; i++) {
    DhtSlot& s = dhts[i];
    if (s.ultima && (millis() - s.ultima) < DHT_EVERY_MS) continue;
    s.ultima = millis();
    float t = s.dht->readTemperature();
    float h = s.dht->readHumidity();
    s.ok = !isnan(t) && !isnan(h);
    if (s.ok) { s.t = t; s.h = h; }
  }
  for (int i = 0; i < n; i++) {
    Canal& c = canales[i];
    if (c.kind != Kind::DhtTemp && c.kind != Kind::DhtHum) continue;
    for (int k = 0; k < nDhts; k++) {
      if (dhts[k].pin != c.pin || !dhts[k].ok) continue;
      // Solo se procesa una vez por lectura nueva.
      if (c.tieneValor && dhts[k].ultima == c.inCambio) continue;
      c.inCambio = dhts[k].ultima;
      if (c.kind == Kind::DhtTemp) publicarValor(c, dhts[k].t, 0.3f);
      else publicarValor(c, dhts[k].h, 1.0f);
    }
  }
}

// ---- Ciclo principal ----

void canalesLoop() {
  uint32_t ahora = millis();
  for (int i = 0; i < n; i++) {
    Canal& c = canales[i];
    if (c.kind == Kind::Rele) {
      if (c.finPulso) { c.finPulso = false; if (cbEstado) cbEstado(c, "off"); }
    } else if (c.kind == Kind::Entrada) {
      loopEntrada(c);
    }
  }
  if (ahora - ultimoAdc >= ADC_EVERY_MS) {
    ultimoAdc = ahora;
    for (int i = 0; i < n; i++) if (canales[i].kind == Kind::Adc) loopAdc(canales[i]);
  }
  loopDht();
  canalesGuardarCiclos(false);
}

void canalesPublicarTodo() {
  for (int i = 0; i < n; i++) {
    Canal& c = canales[i];
    if (c.kind == Kind::Rele) {
      if (cbEstado) cbEstado(c, c.activo ? "on" : "off");
    } else if (c.kind == Kind::Entrada) {
      if (c.inInit) publicarDigital(c);
    } else if (c.tieneValor) {
      char buf[16];
      fmt(buf, sizeof(buf), c.valor);
      if (cbEstado) cbEstado(c, buf);
    }
  }
}

// ---- Contadores de desgaste ----

void canalesCiclosJson(JsonObject out) {
  for (int i = 0; i < n; i++) if (canales[i].kind == Kind::Rele) out[canales[i].id] = canales[i].ciclos;
}

// La flash tiene un numero limitado de escrituras: se guarda cada 10 activaciones o, si hay
// cambios pendientes, cada 10 minutos (y siempre antes de reiniciar por una actualizacion).
void canalesGuardarCiclos(bool forzar) {
  if (!cicloSucio) return;
  if (!forzar && cicloSucio < 10 && (millis() - ultimoGuardado) < CICLOS_SAVE_EVERY_MS) return;
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  canalesCiclosJson(o);
  String s;
  serializeJson(doc, s);
  storeSaveCiclos(s);
  cicloSucio = 0;
  ultimoGuardado = millis();
}
