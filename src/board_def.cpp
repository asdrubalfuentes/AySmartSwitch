#include "board_def.h"

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

WiFiClient netClient;
MQTTPubSubClient mqtt;

static AsyncWebServer server(80);

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
    pinMode(LED_BUILTIN, OUTPUT);
    // Ensure known state
    digitalWrite(RELAY_PIN, LOW);
#if defined(ESP8266)
    // LED usually active low on ESP-01S
    digitalWrite(LED_BUILTIN, HIGH);
#else
    digitalWrite(LED_BUILTIN, LOW);
#endif
}

void ledOn() {
#if defined(ESP8266)
    digitalWrite(LED_BUILTIN, LOW);
#else
    digitalWrite(LED_BUILTIN, HIGH);
#endif
}

void ledOff() {
#if defined(ESP8266)
    digitalWrite(LED_BUILTIN, HIGH);
#else
    digitalWrite(LED_BUILTIN, LOW);
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

void setupOTA() {
    // Endpoint simple de version
    server.on("/version", HTTP_GET, [](AsyncWebServerRequest* request) {
        String body = String("{\n  \"version\": \"") + getFwVersion() + "\",\n  \"short\": \"" + getFwVersionShort() + "\"\n}";
        request->send(200, "application/json", body);
    });

    AsyncElegantOTA.begin(&server);
    server.begin();
}

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

static void subscribeTopics() {
    // Log all messages
    mqtt.subscribe([](const String& topic, const String& payload, const size_t size) {
        Serial.println("mqtt received: " + topic + " - " + payload);
    });

    // Command handler for relay
    mqtt.subscribe(TOPICO_SUB, [](const String& payload, const size_t size) {
        Serial.print(TOPICO_SUB);
        Serial.println(payload);
        if (payload == String("Rele Pulsado")) {
            mqtt.publish(TOPICO_PUB, "Puerta 1");
            digitalWrite(RELAY_PIN, HIGH);
            delay(200);
            digitalWrite(RELAY_PIN, LOW);
        }
    });
}

void connect() {
    ensureWiFiConnected();
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    Serial.print("connecting to host...");
    netClient.stop();
    uint8_t tries = 0;
    while (!netClient.connect(mqttServer, mqttPort)) {
        Serial.print(".");
        delay(500);
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(" WiFi disconnected");
            ensureWiFiConnected();
        }
        if (++tries > 60) { // ~30s
            Serial.println(" broker timeout");
            return;
        }
        yield();
    }
    Serial.println(" connected!");

    Serial.print("connecting to mqtt broker...");
    mqtt.disconnect();
    mqtt.begin(netClient);
    tries = 0;
    while (!mqtt.connect(idUnico, "", "")) {
        Serial.print(".");
        delay(500);
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(" WiFi disconnected");
            ensureWiFiConnected();
        }
        if (!netClient.connected()) {
            Serial.println(" TCP disconnected, retry host");
            return;
        }
        if (++tries > 60) {
            Serial.println(" mqtt timeout");
            return;
        }
        yield();
    }
    Serial.println(" connected!");
    ledOff();

    // Subscribe and publish heartbeat
    subscribeTopics();
    mqtt.publish(TOPICO_PUB_HEARTBIT, "up");
}

void mqttUpdate() {
    mqtt.update();
}

static String httpGet(const char* url) {
    HTTPClient http;
    http.begin(url);
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
    http.begin(firmwareURL);
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
                Serial.printf("Error de actualizacion: %s\n", Update.errorString());
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
