#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// impostazioni led
const int PIN_LED_VERDE = 2;
const int PIN_LED_GIALLO = 4;
const int PIN_LED_ROSSO = 5;
const int DURATA_VERDE = 5000;
const int DURATA_GIALLO = 2500;
const int DURATA_ROSSO = 5000;
const int DURATA_INATTIVO = 500;
enum stato {
    SPENTO, ATTIVO, INATTIVO
};

// impostazioni wifi
const char* WIFI_SSID = "iPhone di Christian";
const char* WIFI_PASSWORD = "montagna";

// impostazioni cluster
const char* CLUSTER_URL = "bf89bd34a48e407abd232255e6194d47.s1.eu.hivemq.cloud";
const int CLUSTER_PORT = 8883;
const char* CLUSTER_USERNAME = "DevEsp";
const char* CLUSTER_PASSWORD = "DevEsp123";
const char* CLUSTER_TOPIC_COMANDI = "esp/comandi";
const char* CLUST_TOPIC_DATI = "esp/dati";

// variabili in runtime
stato statoAttuale = INATTIVO;
unsigned long millisPrecedenti = 0;
int subStatoAttuale = 0;
WiFiClientSecure espClient;
PubSubClient client(espClient);

void gestisciStato() {
    unsigned long millisAttuali = millis();

    switch (statoAttuale) {
        case SPENTO:
            digitalWrite(PIN_LED_VERDE, LOW);
            digitalWrite(PIN_LED_GIALLO, LOW);
            digitalWrite(PIN_LED_ROSSO, LOW);

            subStatoAttuale = 0;

            break;

        case ATTIVO:
            if (subStatoAttuale == 0) {
                digitalWrite(PIN_LED_VERDE, HIGH);
                digitalWrite(PIN_LED_GIALLO, LOW);
                digitalWrite(PIN_LED_ROSSO, LOW);

                subStatoAttuale = 1;
                millisPrecedenti = millisAttuali;
            }
            else if (subStatoAttuale == 1 && millisAttuali - millisPrecedenti >= DURATA_VERDE) {
                digitalWrite(PIN_LED_VERDE, LOW);
                digitalWrite(PIN_LED_GIALLO, HIGH);

                subStatoAttuale = 2;
                millisPrecedenti = millisAttuali;
            }
            else if (subStatoAttuale == 2 && millisAttuali - millisPrecedenti >= DURATA_GIALLO) {
                digitalWrite(PIN_LED_GIALLO, LOW);
                digitalWrite(PIN_LED_ROSSO, HIGH);

                subStatoAttuale = 3;
                millisPrecedenti = millisAttuali;
            }
            else if (subStatoAttuale == 3 && millisAttuali - millisPrecedenti >= DURATA_ROSSO) {
                subStatoAttuale = 0;
            }

            break;

        case INATTIVO:
            if (millisAttuali - millisPrecedenti >= DURATA_INATTIVO) {
                digitalWrite(PIN_LED_VERDE, LOW);
                digitalWrite(PIN_LED_GIALLO, !digitalRead(PIN_LED_GIALLO));
                digitalWrite(PIN_LED_ROSSO, LOW);

                millisPrecedenti = millisAttuali;
            }
            subStatoAttuale = 0;

            break;
    }
}

void setup_wifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
    String messaggio = "";
    for (unsigned int i = 0; i < length; i++) {
        messaggio += (char)payload[i];
    }

    if (messaggio == "spento") {
        statoAttuale = SPENTO;
    }
    else if (messaggio == "attivo") {
        statoAttuale = ATTIVO;
    }
    else if (messaggio == "inattivo") {
        statoAttuale = INATTIVO;
    }
}

void reconnect() {
    while (!client.connected()) {
        String clientId = "ESP-Client-";
        clientId += String(random(0xffff), HEX);

        if (client.connect(clientId.c_str(), CLUSTER_USERNAME, CLUSTER_PASSWORD)) {
            client.subscribe(CLUSTER_TOPIC_COMANDI);
        } else {
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_LED_VERDE, OUTPUT);
    pinMode(PIN_LED_GIALLO, OUTPUT);
    pinMode(PIN_LED_ROSSO, OUTPUT);

    setup_wifi();

    espClient.setInsecure();
    client.setServer(CLUSTER_URL, CLUSTER_PORT);
    client.setCallback(callback);
}

void loop() {
    gestisciStato();

    if (!client.connected()) {
        reconnect();
    }

    client.loop();
}