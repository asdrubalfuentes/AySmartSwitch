#include "actualizacion.h"
#include "certs.h"
#include "canales.h"
#include "enlace_mqtt.h"
#include "net.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#include <esp_task_wdt.h>

namespace {

void urlAsset(const char* owner, const char* repo, const char* asset, char* url, size_t n) {
  snprintf(url, n, "https://github.com/%s/%s/releases/latest/download/%s", owner, repo, asset);
}

// GitHub responde 302 hacia el host de assets: hay que seguir la redireccion.
// No se valida el certificado de GitHub (cambia de CA); la autenticidad la da la firma del firmware.
void prepararHttp(HTTPClient& http, WiFiClientSecure& sec, const char* url) {
  sec.setInsecure();
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(20000);
  http.setReuse(false);
  http.begin(sec, url);
}

String getTexto(const char* url) {
  WiFiClientSecure sec;
  HTTPClient http;
  prepararHttp(http, sec, url);
  int codigo = http.GET();
  String cuerpo = codigo == HTTP_CODE_OK ? http.getString() : String();
  http.end();
  cuerpo.trim();
  return cuerpo;
}

// firmware.sig es binario (firma DER, ~72 bytes).
size_t getBinarioCorto(const char* url, uint8_t* out, size_t max) {
  WiFiClientSecure sec;
  HTTPClient http;
  prepararHttp(http, sec, url);
  size_t leidos = 0;
  if (http.GET() == HTTP_CODE_OK) {
    WiFiClient* flujo = http.getStreamPtr();
    uint32_t ini = millis();
    while ((http.connected() || flujo->available()) && leidos < max && (millis() - ini) < 15000) {
      int disp = flujo->available();
      if (disp > 0) leidos += flujo->readBytes(out + leidos, min((size_t)disp, max - leidos));
      else delay(5);
    }
  }
  http.end();
  return leidos;
}

int semver(const char* s, int v[3]) {
  while (*s == 'v' || *s == 'V' || *s == ' ') s++;
  v[0] = v[1] = v[2] = 0;
  return sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]);
}

bool firmaValida(const uint8_t digest[32], const uint8_t* firma, size_t largo) {
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int rc = mbedtls_pk_parse_public_key(&pk, (const unsigned char*)FW_PUBKEY_PEM, strlen(FW_PUBKEY_PEM) + 1);
  if (rc == 0) rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, 32, firma, largo);
  mbedtls_pk_free(&pk);
  return rc == 0;
}

void fallo(ResultadoOta& r, const char* msg) {
  snprintf(r.error, sizeof(r.error), "%s", msg);
  r.ok = false;
  Serial.printf("[ota] ERROR: %s\n", msg);
}

}  // namespace

bool versionMasNueva(const char* latest, const char* current) {
  int l[3], c[3];
  if (semver(latest, l) != 3 || semver(current, c) != 3) return false;
  for (int i = 0; i < 3; i++) if (l[i] != c[i]) return l[i] > c[i];
  return false;
}

ResultadoOta actualizarSiHayNueva(const char* owner, const char* repo, const char* versionActual) {
  ResultadoOta r;
  if (WiFi.status() != WL_CONNECTED) { fallo(r, "sin WiFi"); return r; }

  char url[160];
  urlAsset(owner, repo, "version.txt", url, sizeof(url));
  String ultima = getTexto(url);
  if (!ultima.length()) { fallo(r, "no se pudo leer version.txt"); return r; }
  strncpy(r.ultima, ultima.c_str(), sizeof(r.ultima) - 1);
  r.ok = true;
  r.hayNueva = versionMasNueva(r.ultima, versionActual);
  Serial.printf("[ota] actual %s, ultima %s\n", versionActual, r.ultima);
  if (!r.hayNueva) return r;

  if (ESP.getFreeHeap() < 60000) { fallo(r, "memoria insuficiente para actualizar"); return r; }

  // 1) SHA-256 y firma esperados (se piden ANTES de bajar el binario).
  urlAsset(owner, repo, "firmware.sha256", url, sizeof(url));
  String esperado = getTexto(url);
  esperado.toLowerCase();
  if (esperado.length() != 64) { fallo(r, "sha256 no disponible"); return r; }

  uint8_t firma[80];
  urlAsset(owner, repo, "firmware.sig", url, sizeof(url));
  size_t largoFirma = getBinarioCorto(url, firma, sizeof(firma));
  if (largoFirma < 8) { fallo(r, "firma no disponible: no se instala"); return r; }

  // 2) Descarga escribiendo en la particion libre y calculando el SHA-256 sobre la marcha.
  mqttDesconectar();
  canalesGuardarCiclos(true);

  urlAsset(owner, repo, "firmware.bin", url, sizeof(url));
  WiFiClientSecure sec;
  HTTPClient http;
  prepararHttp(http, sec, url);
  if (http.GET() != HTTP_CODE_OK) { http.end(); fallo(r, "descarga de firmware.bin fallo"); return r; }
  int total = http.getSize();
  if (!Update.begin(total > 0 ? (size_t)total : UPDATE_SIZE_UNKNOWN)) { http.end(); fallo(r, "Update.begin fallo"); return r; }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);

  WiFiClient* flujo = http.getStreamPtr();
  uint8_t buf[1024];
  size_t escritos = 0;
  uint32_t ultimoDato = millis();
  bool error = false;
  while (true) {
    size_t disp = flujo->available();
    if (disp) {
      int n = flujo->readBytes(buf, disp < sizeof(buf) ? disp : sizeof(buf));
      if (n <= 0 || (int)Update.write(buf, n) != n) { error = true; break; }
      mbedtls_sha256_update(&sha, buf, n);
      escritos += n;
      ultimoDato = millis();
      esp_task_wdt_reset();
      if (total > 0 && escritos >= (size_t)total) break;
    } else {
      if (!flujo->connected()) break;
      if (millis() - ultimoDato > 20000) { error = true; break; }
      delay(1);
    }
  }
  http.end();

  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);

  if (error || (total > 0 && escritos != (size_t)total)) { Update.abort(); fallo(r, "descarga incompleta"); return r; }
  if (esperado != String(hex)) { Update.abort(); fallo(r, "el sha256 no coincide"); return r; }
  if (!firmaValida(digest, firma, largoFirma)) { Update.abort(); fallo(r, "FIRMA INVALIDA: firmware rechazado"); return r; }

  if (!Update.end(true) || !Update.isFinished()) { fallo(r, "Update.end fallo"); return r; }
  Serial.println("[ota] firmware verificado e instalado; reiniciando");
  delay(400);
  ESP.restart();
  return r;
}
