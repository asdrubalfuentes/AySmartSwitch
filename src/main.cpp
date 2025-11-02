#include "board_def.h"

void setup() {
	delay(5000);
	Serial.begin(115200);
	initBoard();
	Serial.println("Iniciando...");
	beginWiFi(ssid, pass);

	Serial.println(String("aySmartSwitch-") + idUnico + " FW " + getFwVersion());

	setupOTA();
	chequearActualizaciones();
	connect();
}

void loop() {
	// Entrada por serial para configurar WiFi y prueba de relé
	if (Serial.available() > 0) {
		char a = (char)Serial.read();
		if (a == 's') {
			ssid = Serial.readStringUntil('\n');
		} else if (a == 'p') {
			pass = Serial.readStringUntil('\n');
			beginWiFi(ssid, pass);
		} else if (a == '1') {
			digitalWrite(RELE, HIGH);
			delay(5000);
			digitalWrite(RELE, LOW);
		}
	}

	if (!mqttIsConnected()) {
		ledOn();
		connect();
	}

	mqttUpdate();

	static uint32_t prev_ms = millis();
	if (millis() - prev_ms > 1000) {
		prev_ms = millis();
		// espacio para heartbeats u otras tareas periódicas
	}
}

/* Blink de prueba
#include <Arduino.h>
void setup() {
	Serial.begin(115200);
	Serial.println("Hola Mundo a 115200 baudios");
	pinMode(0, OUTPUT);
}

void loop() {
	digitalWrite(0, LOW);
	delay(200);
	digitalWrite(0, HIGH);
	delay(1000);
}*/