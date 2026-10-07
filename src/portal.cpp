#include "portal.h"
#include "config.h"
#include "store.h"
#include "net.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <vector>

static WebServer web(80);
static DNSServer dns;
static bool activo = false;
static bool porFalla = false;
static bool pedirActualizar = false;
static bool reiniciar = false;
static uint32_t inicio = 0;
static uint32_t reiniciarEn = 0;
static String mensaje;

void portalMensaje(const String& texto) { mensaje = texto; }

// Redes WiFi vistas al abrir el portal (las ofrece la pagina para elegir).
static std::vector<String> redesCache;

static void buscarRedes() {
  Serial.println("[portal] buscando redes WiFi...");
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);   // el escaneo exige la parte STA
  else WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false);
  redesCache.clear();
  for (int i = 0; i < n && redesCache.size() < 20; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool repetida = false;
    for (const String& e : redesCache) if (e == s) { repetida = true; break; }
    if (!repetida) redesCache.push_back(s);
  }
  WiFi.scanDelete();
  Serial.printf("[portal] %u redes encontradas\n", (unsigned)redesCache.size());
}

String portalSsid() { return String("AySmartSwitch-") + String(deviceId + 8); }   // ultimos 4 hex
String portalClave() { return String(deviceId + 4); }                              // ultimos 8 hex

static String esc(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;"; else if (c == '\'') o += "&#39;"; else o += c;
  }
  return o;
}

static String pagina(const String& cuerpo) {
  return String("<!doctype html><html lang=es><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'><title>aySmartSwitch</title><style>"
    "body{margin:0;padding:18px;background:#0d2830;color:#eaf2f0;font-family:system-ui,Arial,sans-serif;max-width:460px;margin:auto}"
    "h1{margin:6px 0 14px;font-size:1.5rem}.card{background:#14313c;border:1px solid #2b4a55;border-radius:14px;padding:14px;margin-bottom:14px}"
    "label{display:block;margin:12px 0 4px;font-size:.85rem;color:#8aa29c}"
    "input,select{width:100%;box-sizing:border-box;padding:11px;border-radius:10px;border:1px solid #2b4a55;background:#0a1e24;color:#eaf2f0;font-size:1rem}"
    "button{width:100%;margin-top:14px;padding:12px;border:0;border-radius:12px;background:#f77535;color:#fff;font-size:1rem;font-weight:600}"
    "button.sec{background:#14313c;border:1px solid #2b4a55}button.danger{background:#e5484d}small{color:#8aa29c}"
    "</style></head><body><h1>aySmartSwitch</h1>") + cuerpo + "</body></html>";
}

static String paginaInicio() {
  String estado;
  if (cfg.enrolled) estado = "Adherido a un instrumento";
  else estado = cfg.code[0] ? "Codigo de adhesion recibido: se adherira al conectarse" : "Sin adherir: falta el codigo de adhesion";
  String h = "<div class=card><b>Equipo " + String(deviceId) + "</b><br><small>Firmware " + FW_VERSION + " &middot; " + estado + "<br>WiFi: ";
  if (mensaje.length()) h = "<div class=card style='border-color:#f77535'>" + esc(mensaje) + "</div>" + h;
  h += netConectado() ? ("conectado a " + esc(WiFi.SSID()) + " (" + String(WiFi.RSSI()) + " dBm)") : "sin conexion";
  h += "</small></div><form method=post action=/guardar><label>Red WiFi del sitio</label>"
       "<select id=redes onchange=\"document.getElementById('ssid').value=this.value\"><option value=''>Buscando redes...</option></select>"
       "<input name=ssid id=ssid placeholder='Nombre de la red' maxlength=32 value='" + esc(cfg.ssid) + "'>"
       "<label>Contrasena del WiFi</label><input name=pass type=password maxlength=64 placeholder='" + String(cfg.ssid[0] ? "(sin cambios)" : "") + "'>";
  if (!cfg.enrolled) {
    h += "<label>Codigo de adhesion (lo genera tu administrador)</label>"
         "<input name=code placeholder='ABCD-EFGH' maxlength=9 autocapitalize=characters autocomplete=off>";
  }
  h += "<button>Guardar y conectar</button></form>"
       "<form method=post action=/actualizar><button class=sec>Buscar actualizacion de firmware</button></form>"
       "<details style='margin-top:18px'><summary>Restaurar de fabrica</summary>"
       "<form method=post action=/reset onsubmit=\"return confirm('Se borra la configuracion y la adhesion. Continuar?')\">"
       "<button class=danger>Borrar todo</button></form></details>"
       "<script>(async function c(){const r=await(await fetch('/scan')).json();const s=document.getElementById('redes');"
       "if(r.cargando){setTimeout(c,1500);return}s.innerHTML='<option value=\"\">Elige una red...</option>'+r.redes.map(function(n){"
       "var o=document.createElement('option');o.textContent=n;return o.outerHTML}).join('')})();</script>";
  return pagina(h);
}

static void redirigir() {
  Serial.printf("[portal] %s%s -> redirige a la pagina de configuracion\n", web.hostHeader().c_str(), web.uri().c_str());
  web.sendHeader("Location", "http://192.168.4.1/", true);
  web.send(302, "text/plain", "");
}

static void alGuardar() {
  String ssid = web.arg("ssid");
  String pass = web.arg("pass");
  String code = web.arg("code");
  ssid.trim();

  String codigo;
  for (size_t i = 0; i < code.length(); i++) if (isalnum((unsigned char)code[i])) codigo += (char)toupper(code[i]);
  if (code.length() && codigo.length() != 8) {
    web.send(400, "text/html; charset=utf-8", pagina("<div class=card>El codigo de adhesion debe tener 8 caracteres (por ejemplo ABCD-EFGH).</div><a href=/>Volver</a>"));
    return;
  }
  if (!ssid.length() && !cfg.ssid[0]) {
    web.send(400, "text/html; charset=utf-8", pagina("<div class=card>Elige o escribe la red WiFi.</div><a href=/>Volver</a>"));
    return;
  }
  if (ssid.length()) {
    // Misma red y clave en blanco = se conserva la clave actual.
    if (!pass.length() && ssid == String(cfg.ssid)) pass = cfg.pass;
    storeSaveWifi(ssid.c_str(), pass.c_str());
  }
  if (codigo.length()) storeSaveCode(codigo.c_str());

  web.send(200, "text/html; charset=utf-8", pagina("<div class=card><b>Guardado.</b><br>El equipo se reinicia y se conecta a tu WiFi. "
    "Si es la primera vez, en un minuto quedara adherido a su instrumento y podras cerrar esta pagina.</div>"));
  reiniciar = true;
  reiniciarEn = millis() + 1500;
}

void portalIniciar(bool porFallaWifi) {
  if (activo) return;
  porFalla = porFallaWifi;
  inicio = millis();
  // Las redes se buscan ANTES de abrir el portal: escanear con el portal abierto obliga al radio a
  // saltar de canal y la red del equipo "desaparece" unos segundos justo cuando el celular carga la pagina.
  buscarRedes();
  // AP + STA: el equipo sigue intentando su WiFi mientras el portal esta abierto.
  WiFi.mode(cfg.ssid[0] ? WIFI_AP_STA : WIFI_AP);
  // Canal 6 (el 1 suele estar saturado), maximo 4 celulares conectados.
  WiFi.softAP(portalSsid().c_str(), portalClave().c_str(), 6, 0, 4);
  netAjustarRadio();
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());

  web.on("/", HTTP_GET, []() { Serial.println("[portal] pagina principal pedida"); web.send(200, "text/html; charset=utf-8", paginaInicio()); });
  web.on("/scan", HTTP_GET, []() {
    // Por defecto entrega la lista ya buscada. "?nuevo=1" vuelve a buscar (el portal se corta unos segundos).
    if (web.hasArg("nuevo")) buscarRedes();
    JsonDocument d;
    d["cargando"] = false;
    JsonArray redes = d["redes"].to<JsonArray>();
    for (const String& s : redesCache) redes.add(s);
    String s;
    serializeJson(d, s);
    web.send(200, "application/json", s);
  });
  web.on("/guardar", HTTP_POST, alGuardar);
  web.on("/actualizar", HTTP_POST, []() {
    pedirActualizar = true;
    web.send(200, "text/html; charset=utf-8", pagina("<div class=card>Buscando actualizacion... si hay una nueva se instala y el equipo se reinicia.</div><a href=/>Volver</a>"));
  });
  web.on("/reset", HTTP_POST, []() {
    web.send(200, "text/html; charset=utf-8", pagina("<div class=card>Restaurando de fabrica...</div>"));
    delay(300);
    storeFactoryReset();
  });
  // Deteccion de portal cautivo de Android, iOS y Windows: todo redirige a la pagina de configuracion.
  web.onNotFound(redirigir);
  web.begin();
  activo = true;
  Serial.printf("[portal] red %s (clave %s) en http://192.168.4.1\n", portalSsid().c_str(), portalClave().c_str());
}

void portalDetener() {
  if (!activo) return;
  web.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  activo = false;
}

bool portalActivo() { return activo; }

bool portalPidioActualizar() { bool v = pedirActualizar; pedirActualizar = false; return v; }

void portalLoop() {
  if (!activo) return;
  dns.processNextRequest();
  web.handleClient();
  if (reiniciar && (int32_t)(millis() - reiniciarEn) >= 0) ESP.restart();
  // Si se abrio solo porque no habia WiFi, no se queda abierto para siempre.
  if (porFalla && (millis() - inicio) > PORTAL_TIMEOUT_MS) portalDetener();
}
