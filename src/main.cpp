#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <SPIFFS.h>
#include "AudioFileSourceSPIFFS.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2S.h"

// impostazioni dati
const int DURATA_DATI = 5000;
unsigned long millisAttuali = 0;
unsigned long millisPrecedentiDati = 0;

// impostazioni stato
enum stato {
    SPENTO, ATTIVO, INATTIVO
};

stato statoAttuale = ATTIVO;
int subStatoAttuale = 0;

// impostazioni led
const int PIN_LED_VERDE = 19;
const int PIN_LED_GIALLO = 18;
const int PIN_LED_ROSSO = 5;

const int DURATA_VERDE = 4000;
const int DURATA_GIALLO = 2000;
const int DURATA_ROSSO = 4000;
const int DURATA_INATTIVO = 500;

// Durata rosso che cambia al rilevamento RFID
int DURATA_ROSSO_ATTUALE = DURATA_ROSSO;

unsigned long millisPrecedentiLED = 0;

// impostazioni HC-SR04
const int PIN_TRIG = 16;
const int PIN_ECHO = 17;

const int DURATA_LETTURA_DISTANZA = 500;
const int MASSIMA_DISTANZA = 15;

const int VEICOLI_TRAFFICO_MEDIO = 4;
const int VEICOLI_TRAFFICO_ALTO = 8;

unsigned long millisPrecedentiHCSR04 = 0;

bool veicoli[10] = {false};
int indiceVeicoli = 0;

// impostazioni pulsante
const int PIN_PULSANTE = 23;
bool statoPrecedentePulsante = HIGH;

// impostazioni DHT22
const int PIN_DHT = 4;
DHT dht(PIN_DHT, DHT22);

// impostazioni wifi
const char* WIFI_SSID = "iPhone di Christian";
const char* WIFI_PASSWORD = "montagna";

WiFiClientSecure espClient;

// impostazioni cluster
const char* CLUSTER_URL = "bf89bd34a48e407abd232255e6194d47.s1.eu.hivemq.cloud";
const int CLUSTER_PORT = 8883;

const char* CLUSTER_USERNAME = "DevEsp";
const char* CLUSTER_PASSWORD = "DevEsp123";

const char* CLUSTER_TOPIC_COMANDI = "esp/comandi";
const char* CLUSTER_TOPIC_DATI = "esp/dati";
const char* CLUSTER_TOPIC_LUCE = "esp/luce";
const char* CLUSTER_TOPIC_RFID = "esp/rfid";
const char* CLUSTER_TOPIC_RFID_RISPOSTA = "esp/rfid/risposta";

PubSubClient client(espClient);

// impostazioni HW-072 (luminosità)
const int PIN_LUMINOSITA = 35;

// impostazioni sensore acqua
const int PIN_ACQUA = 34;
const int SOGLIA_ACQUA = 250;

// --- DEFINIZIONE PIN RFID ---
#define PIN_RFID_SCK 32
#define PIN_RFID_MISO 25
#define PIN_RFID_MOSI 33
#define PIN_RFID_SS 26
#define PIN_RFID_RST 27
// --- ISTANZIAZIONE OGGETTO ---
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);

// impostazioni speaker
AudioFileSourceSPIFFS* audioSource = nullptr;
AudioGeneratorWAV* audioWav = nullptr;
AudioOutputI2S* audioOutput = nullptr;

bool audioInRiproduzione = false;

int wrap(int amt, int min, int max) {
    int range = max - min;
    return (amt - min) % range + min;
}

void setup_wifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.setSleep(false);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
}

void gestisciLed() {
    String payload;

    switch (statoAttuale) {
        case SPENTO:
            digitalWrite(PIN_LED_VERDE, LOW);
            digitalWrite(PIN_LED_GIALLO, LOW);
            digitalWrite(PIN_LED_ROSSO, LOW);
            subStatoAttuale = 0;
            payload = "spento";
            client.publish(CLUSTER_TOPIC_LUCE, payload.c_str());
            break;

        case ATTIVO:
            if (subStatoAttuale == 0) {
                digitalWrite(PIN_LED_VERDE, HIGH);
                digitalWrite(PIN_LED_GIALLO, LOW);
                digitalWrite(PIN_LED_ROSSO, LOW);
                subStatoAttuale = 1;
                millisPrecedentiLED = millisAttuali;
                payload = "verde";
                client.publish(CLUSTER_TOPIC_LUCE, payload.c_str());
            }
            else if (subStatoAttuale == 1 && millisAttuali - millisPrecedentiLED >= DURATA_VERDE) {
                digitalWrite(PIN_LED_VERDE, LOW);
                digitalWrite(PIN_LED_GIALLO, HIGH);
                subStatoAttuale = 2;
                millisPrecedentiLED = millisAttuali;
                payload = "giallo";
                client.publish(CLUSTER_TOPIC_LUCE, payload.c_str());
            }
            else if (subStatoAttuale == 2 && millisAttuali - millisPrecedentiLED >= DURATA_GIALLO) {
                digitalWrite(PIN_LED_GIALLO, LOW);
                digitalWrite(PIN_LED_ROSSO, HIGH);
                subStatoAttuale = 3;
                millisPrecedentiLED = millisAttuali;
                payload = "rosso";
                client.publish(CLUSTER_TOPIC_LUCE, payload.c_str());

                Serial.printf("[ROSSO] Durata: %dms\n", DURATA_ROSSO_ATTUALE);
            }
            else if (subStatoAttuale == 3 && millisAttuali - millisPrecedentiLED >= DURATA_ROSSO_ATTUALE) {
                subStatoAttuale = 0;
                millisPrecedentiLED = millisAttuali;
                DURATA_ROSSO_ATTUALE = DURATA_ROSSO; // Reset alla durata standard
            }
            break;

        case INATTIVO:
            if (millisAttuali - millisPrecedentiLED >= DURATA_INATTIVO) {
                digitalWrite(PIN_LED_VERDE, LOW);
                digitalWrite(PIN_LED_GIALLO, !digitalRead(PIN_LED_GIALLO));
                digitalWrite(PIN_LED_ROSSO, LOW);
                millisPrecedentiLED = millisAttuali;
                payload = (digitalRead(PIN_LED_GIALLO) == LOW) ? "spento" : "giallo";
                client.publish(CLUSTER_TOPIC_LUCE, payload.c_str());
            }
            subStatoAttuale = 0;
            break;
    }
}

void gestisciHCSR04() {
    if (subStatoAttuale != 3 && millisAttuali - millisPrecedentiHCSR04 >= DURATA_LETTURA_DISTANZA) {
        digitalWrite(PIN_TRIG, LOW);
        delayMicroseconds(2);
        digitalWrite(PIN_TRIG, HIGH);
        delayMicroseconds(10);
        digitalWrite(PIN_TRIG, LOW);

        long durata = pulseIn(PIN_ECHO, HIGH, 30000);
        float distanza = (durata * 0.0343) / 2.0;

        veicoli[indiceVeicoli] = (distanza > 0 && distanza < MASSIMA_DISTANZA);
        indiceVeicoli = wrap(indiceVeicoli + 1, 0, 10);
        millisPrecedentiHCSR04 = millisAttuali;
    }
}

void validateRFID() {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
        String uid = "";
        for (byte i = 0; i < rfid.uid.size; i++) {
            if (rfid.uid.uidByte[i] < 0x10) uid += "0";
            uid += String(rfid.uid.uidByte[i], HEX);
            if (i < rfid.uid.size - 1) uid += ":";
        }
        uid.toUpperCase();

        client.publish(CLUSTER_TOPIC_RFID, uid.c_str());
        // L'aumento della durata è stato rimosso da qui. Verrà gestito dal callback MQTT.

        rfid.PICC_HaltA();
    }
}

void gestisciPulsante() {
    bool stato = digitalRead(PIN_PULSANTE);
    if (statoPrecedentePulsante == HIGH && stato == LOW) {
        if (subStatoAttuale == 1) {
            digitalWrite(PIN_LED_VERDE, LOW);
            digitalWrite(PIN_LED_GIALLO, HIGH);
            subStatoAttuale = 2;
            millisPrecedentiLED = millisAttuali;

            String payload = "giallo";
            client.publish(CLUSTER_TOPIC_LUCE, payload.c_str());
        }
    }
    statoPrecedentePulsante = stato;
}

void gestisciDati() {
    JsonDocument payloadJson;
    float umidita = dht.readHumidity();
    float temperatura = dht.readTemperature();
    bool luminosita = analogRead(PIN_LUMINOSITA) < 100;
    String traffico;
    int veicoliTotali = 0;

    for (int i = 0; i < 10; i++) {
        if (veicoli[i]) veicoliTotali++;
    }

    if (veicoliTotali >= VEICOLI_TRAFFICO_ALTO)          traffico = "alto";
    else if (veicoliTotali >= VEICOLI_TRAFFICO_MEDIO)    traffico = "medio";
    else                                                 traffico = "basso";

    payloadJson["traffico"] = traffico;
    payloadJson["umidita"] = umidita;
    payloadJson["temperatura"] = temperatura;
    payloadJson["luminosita"] = luminosita; // Modificato: invia il valore ADC reale (0-4095) invece del booleano fasullo
    payloadJson["acqua"] = (analogRead(PIN_ACQUA) > SOGLIA_ACQUA);

    String payloadString;
    serializeJson(payloadJson, payloadString);
    client.publish(CLUSTER_TOPIC_DATI, payloadString.c_str());
}

void callback(char* topic, byte* payload, unsigned int length) {
    char messaggio[length + 1];
    memcpy(messaggio, payload, length);
    messaggio[length] = '\0';

    if (strcmp(topic, CLUSTER_TOPIC_COMANDI) == 0) {
        if (strcmp(messaggio, "spento") == 0)        statoAttuale = SPENTO;
        else if (strcmp(messaggio, "attivo") == 0)   statoAttuale = ATTIVO;
        else if (strcmp(messaggio, "inattivo") == 0) statoAttuale = INATTIVO;
    }
    else if (strcmp(topic, CLUSTER_TOPIC_RFID_RISPOSTA) == 0) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, messaggio);

        if (!error) {
            int livello = doc["livello"];
            if (livello > 0) {
                DURATA_ROSSO_ATTUALE = DURATA_ROSSO * livello;
                Serial.printf("[RFID] Risposta ricevuta. Nuovo moltiplicatore livello: %d. Prossimo rosso durerà: %dms\n", livello, DURATA_ROSSO_ATTUALE);
            }
        } else {
            Serial.println("[ERRORE] Parsing JSON fallito per risposta RFID");
        }
    }
}

void reconnect() {
    while (!client.connected()) {
        String clientId = "ESP-Client-";
        clientId += String(random(0xffff), HEX);
        if (client.connect(clientId.c_str(), CLUSTER_USERNAME, CLUSTER_PASSWORD)) {
            client.subscribe(CLUSTER_TOPIC_COMANDI);
            client.subscribe(CLUSTER_TOPIC_RFID_RISPOSTA); // Iscrizione al nuovo topic
        } else {
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n[SETUP] Inizializzazione in corso...");

    pinMode(PIN_LED_VERDE, OUTPUT);
    pinMode(PIN_LED_GIALLO, OUTPUT);
    pinMode(PIN_LED_ROSSO, OUTPUT);
    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT);
    pinMode(PIN_PULSANTE, INPUT_PULLUP);

    dht.begin();

    SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_SS);
    rfid.PCD_Init();
    Serial.println("[OK] RFID inizializzato");

    pinMode(PIN_LUMINOSITA, INPUT);
    pinMode(PIN_ACQUA, INPUT);

    SPIFFS.begin(true);
    setup_wifi();
    espClient.setInsecure();

    client.setServer(CLUSTER_URL, CLUSTER_PORT);
    client.setCallback(callback);

    Serial.println("[SETUP] Pronto!");
}

void loop() {
    millisAttuali = millis();

    gestisciLed();
    gestisciHCSR04();
    gestisciPulsante();
    validateRFID();

    if (millisAttuali - millisPrecedentiDati >= DURATA_DATI) {
        gestisciDati();
        millisPrecedentiDati = millisAttuali;
    }

    if (!client.connected()) {
        reconnect();
    }
    client.loop();
}