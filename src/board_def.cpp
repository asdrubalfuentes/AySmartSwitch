#include "board_def.h"
#if defined(ESP8266)
#include <ESP8266HTTPClient.h>
#include <Ticker.h>
#include <Updater.h>
#else
#include <HTTPClient.h>
#include <Ticker.h>
#include <Update.h>
#endif
#include <AsyncMqttClient.h>

// Globals
String ssid = "";
String pass = "";

const char* mqttServer = "emqx.aysafi.com";
const uint16_t mqttPort = 1883;

const char* versionURL  = "https://aysafi.com/AySmartSwitch/index.php";
const char* firmwareURL = "https://aysafi.com/AySmartSwitch/firmware.bin";

String idUnico = "";
String TOPICO_SUB = "";
String TOPICO_PUB = "";
String TOPICO_PUB_HEARTBIT = "";

static AsyncMqttClient mqttClient;
static Ticker mqttReconnectTimer;
static Ticker wifiReconnectTimer;
static bool mqttCallbacksInit = false;

static void computeIdAndTopics() {
#if defined(ESP8266)
    // Prefer Arduino core API
    uint32_t chip = ESP.getChipId();
    char buf[9];
    snprintf(buf, sizeof(buf), "%08X", chip);
    idUnico = String(buf);
#elif defined(ESP32)
    uint64_t chipid = ESP.getEfuseMac();
    // Use lower 32-bits for brevity
    char buf[9];
    snprintf(buf, sizeof(buf), "%08X", (uint32_t)chipid);
    idUnico = String(buf);
#endif
    TOPICO_SUB = String("/") + idUnico + "-comando";
    TOPICO_PUB = String("/") + idUnico + "-respuesta";
    TOPICO_PUB_HEARTBIT = String("/") + idUnico + "-heartBit";
}

void initBoard() {
    computeIdAndTopics();
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    // Ensure known state
    digitalWrite(RELAY_PIN, LOW);
#if defined(ESP8266)
    // LED usually active low on ESP-01S
    digitalWrite(LED_PIN, HIGH);
#else
    digitalWrite(LED_PIN, LOW);
#endif
}

void ledOn() {
#if defined(ESP8266)
    digitalWrite(LED_PIN, LOW);
#else
    digitalWrite(LED_PIN, HIGH);
#endif
}

void ledOff() {
#if defined(ESP8266)
    digitalWrite(LED_PIN, HIGH);
#else
    digitalWrite(LED_PIN, LOW);
#endif
}

void beginWiFi(const String& ssid_, const String& pass_) {
    ssid = ssid_;
    pass = pass_;
    if (ssid.length() > 0) {
#if defined(ESP8266)
        WiFi.mode(WIFI_STA);
#endif
        WiFi.begin(ssid.c_str(), pass.c_str());
    }
}

void setupOTA() { /* no-op, solo OTA remoto via HTTP */ }

static void ensureWiFiConnected() {
    if (WiFi.status() == WL_CONNECTED) return;
    if (ssid.length() == 0) return; // nothing to connect

    Serial.print("connecting to wifi");
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
        if (millis() - start > 15000) { // 15s timeout
            Serial.println(" timeout");
            break;
        }
        yield();
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print(" connected! Ip: ");
        Serial.println(WiFi.localIP());
        Serial.print("DNS: ");
#if defined(ESP8266)
        Serial.println(WiFi.dnsIP(1));
#else
        // ESP32 API: get DNS via tcpip_adapter or leave info minimal
        Serial.println("n/a");
#endif
    }
}

static void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
    String t(topic);
    String p;
    p.reserve(len);
    for (size_t i = 0; i < len; ++i) p += payload[i];
    Serial.println("mqtt received: " + t + " - " + p);
    if (t == TOPICO_SUB) {
        if (p == String("Rele Pulsado")) {
            mqttClient.publish(TOPICO_PUB.c_str(), 0, false, "Puerta 1");
            digitalWrite(RELAY_PIN, HIGH);
            delay(200);
            digitalWrite(RELAY_PIN, LOW);
        }
    }
}

static void tryConnectMqtt();

static void onMqttConnect(bool sessionPresent) {
    Serial.println("MQTT connected");
    ledOff();
    // Subscribe topics
    mqttClient.subscribe(TOPICO_SUB.c_str(), 0);
    // Heartbeat up
    mqttClient.publish(TOPICO_PUB_HEARTBIT.c_str(), 0, false, "up");
}

static void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
    Serial.println("MQTT disconnected");
    ledOn();
    mqttReconnectTimer.once(2.0f, tryConnectMqtt);
}

static void tryConnectMqtt() {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connecting to MQTT...");
        mqttClient.connect();
    } else {
        Serial.println("WiFi not connected, retrying WiFi...");
        WiFi.reconnect();
        mqttReconnectTimer.once(2.0f, tryConnectMqtt);
    }
}

void connect() {
    ensureWiFiConnected();
    if (!mqttCallbacksInit) {
        mqttCallbacksInit = true;
        mqttClient.onConnect(onMqttConnect);
        mqttClient.onDisconnect(onMqttDisconnect);
        mqttClient.onMessage(onMqttMessage);
        mqttClient.setServer(mqttServer, mqttPort);
        mqttClient.setClientId((String("aySmartSwitch-") + idUnico).c_str());
    }
    tryConnectMqtt();
}

void mqttUpdate() {
    // No-op con AsyncMqttClient
}

static String httpGet(const char* url) {
    HTTPClient http;
    #if defined(ESP8266)
    WiFiClient wcli;
    http.begin(wcli, url);
    #else
    http.begin(url);
    #endif
    int httpCode = http.GET();
    String payload;
    if (httpCode == 200) {
        payload = http.getString();
    } else {
        Serial.printf("HTTP GET %s failed: %d\n", url, httpCode);
    }
    http.end();
    return payload;
}

static void updateFirmware() {
    HTTPClient http;
    #if defined(ESP8266)
    WiFiClient wcli;
    http.begin(wcli, firmwareURL);
    #else
    http.begin(firmwareURL);
    #endif
    int httpCode = http.GET();

    if (httpCode == 200) {
        int len = http.getSize();
        if (len > 0) {
            WiFiClient* stream = http.getStreamPtr();
            if (!Update.begin(len)) {
                Serial.println("Error al iniciar la actualizacion");
                http.end();
                return;
            }
            Update.onProgress([](size_t written, size_t total) {
                static uint32_t lastPrint = 0;
                if (millis() - lastPrint > 500) {
                    int progress = total ? (int)((written * 100) / total) : 0;
                    Serial.printf("OTA %d%%\n", progress);
                    lastPrint = millis();
                }
            });
            size_t written = Update.writeStream(*stream);
            if (written == (size_t)len) {
                Serial.println("Escrito correctamente");
            } else {
                Serial.printf("Error al escribir: %u/%d bytes\n", (unsigned)written, len);
            }
            if (Update.end()) {
                Serial.println("Actualizacion finalizada correctamente");
                if (Update.isFinished()) {
                    Serial.println("Reiniciando...");
                    ESP.restart();
                } else {
                    Serial.println("Actualizacion incompleta");
                }
            } else {
                #if defined(ESP8266)
                Serial.print("Error de actualizacion: ");
                Update.printError(Serial);
                Serial.println();
                #else
                Serial.printf("Error de actualizacion: %s\n", Update.errorString());
                #endif
            }
        } else {
            Serial.println("Archivo binario vacio");
        }
    } else {
        Serial.printf("Error en la descarga del firmware: %d\n", httpCode);
    }

    http.end();
}

void chequearActualizaciones() {
    if (WiFi.status() != WL_CONNECTED) return;
    // latest version should be comparable to FW_VERSION_SHORT like "01.02.125"
    String latest = httpGet(versionURL);
    latest.trim();
    if (!latest.length()) return;
    String current = String(getFwVersionShort());
    if (latest > current) {
        Serial.printf("Nueva version disponible (%s > %s), actualizando...\n", latest.c_str(), current.c_str());
        updateFirmware();
    } else {
        Serial.printf("Version al dia: %s\n", current.c_str());
    }
}

const char* getFwVersion() { return FW_VERSION; }
const char* getFwVersionShort() { return FW_VERSION_SHORT; }

bool mqttIsConnected() { return mqttClient.connected(); }
