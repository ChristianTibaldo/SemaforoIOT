#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>                      // --- NUOVO ---
#include <MPU9250.h>                   // --- NUOVO ---
#include <SPI.h>                       // --- NUOVO ---
#include <MFRC522.h>                   // --- NUOVO ---
#include <SPIFFS.h>                    // --- NUOVO ---
#include "AudioFileSourceSPIFFS.h"     // --- NUOVO ---
#include "AudioGeneratorWAV.h"         // --- NUOVO ---
#include "AudioOutputI2S.h"            // --- NUOVO ---

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
const int PIN_LED_VERDE = 2;
const int PIN_LED_GIALLO = 4;
const int PIN_LED_ROSSO = 5;
const int DURATA_VERDE = 4000;
const int DURATA_GIALLO = 2000;
const int DURATA_ROSSO = 4000;
const int DURATA_INATTIVO = 500;
unsigned long millisPrecedentiLED = 0;

// impostazioni HC-SR04
const int PIN_TRIG = 16;
const int PIN_ECHO = 17;
const int DURATA_LETTURA_DISTANZA = 500;
const int MASSIMA_DISTANZA = 15;
const int VEICOLI_TRAFFICO_MEDIO = 5;
const int VEICOLI_TRAFFICO_ALTO = 8;
unsigned long millisPrecedentiHCSR04 = 0;
bool veicoli[10] = {false};
int indiceVeicoli = 0;

// impostazioni pulsante
const int PIN_PULSANTE = 15;
bool statoPrecedentePulsante = HIGH;

// impostazioni DHT22
const int PIN_DHT = 18;
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
const char* CLUSTER_TOPIC_RFID = "esp/rfid"; // --- NUOVO ---
PubSubClient client(espClient);

// impostazioni giroscopio
MPU9250 mpu;

// impostazioni HW-072 (luminosità) [3.3V] --- NUOVO ---
// VCC→3.3V, GND→GND, AO→GPIO34
const int PIN_LUMINOSITA = 34;

// impostazioni sensore acqua [3.3V] --- NUOVO ---
// VCC→3.3V, GND→GND, S→GPIO35
const int PIN_ACQUA = 35;
const int SOGLIA_ACQUA = 500;

// impostazioni RFID-RC522 [3.3V — NON usare 5V, danneggia il modulo] --- NUOVO ---
// VCC→3.3V, GND→GND, SDA(SS)→GPIO27, SCK→GPIO14
// MOSI→GPIO13, MISO→GPIO33, RST→GPIO32
const int PIN_RFID_SS = 27;
const int PIN_RFID_RST = 32;
const int PIN_RFID_SCK = 14;
const int PIN_RFID_MOSI = 13;
const int PIN_RFID_MISO = 33;
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST); // --- NUOVO ---

// impostazioni speaker [DAC interno ESP32] --- NUOVO ---
// Segnale→GPIO25 (DAC0), GND→GND
// File: /data/suono.wav — PCM mono, 16-bit, 16000Hz
// Caricamento: pio run --target uploadfs
AudioFileSourceSPIFFS* audioSource = nullptr;  // --- NUOVO ---
AudioGeneratorWAV* audioWav = nullptr;          // --- NUOVO ---
AudioOutputI2S* audioOutput = nullptr;          // --- NUOVO ---
bool audioInRiproduzione = false;               // --- NUOVO ---

int wrap(int amt, int min, int max) {
    int range = max - min;
    return (amt - min) % range + min;
}

void setup_wifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

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
            }
            else if (subStatoAttuale == 3 && millisAttuali - millisPrecedentiLED >= DURATA_ROSSO) {
                subStatoAttuale = 0;
                millisPrecedentiLED = millisAttuali;
            }

            break;

        case INATTIVO:
            if (millisAttuali - millisPrecedentiLED >= DURATA_INATTIVO) {
                digitalWrite(PIN_LED_VERDE, LOW);
                digitalWrite(PIN_LED_GIALLO, !digitalRead(PIN_LED_GIALLO));
                digitalWrite(PIN_LED_ROSSO, LOW);

                millisPrecedentiLED = millisAttuali;

                if (digitalRead(PIN_LED_GIALLO) == LOW) {
                    payload = "spento";
                }
                else {
                    payload = "giallo";
                }
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
        if (distanza > 0 && distanza < MASSIMA_DISTANZA) {
            veicoli[indiceVeicoli] = true;
        }
        else {
            veicoli[indiceVeicoli] = false;
        }
        indiceVeicoli = wrap(indiceVeicoli + 1, 0, 10);

        millisPrecedentiHCSR04 = millisAttuali;
    }
}

// --- NUOVO ---
void gestisciRFID() {
    if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

    String uid = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
        if (rfid.uid.uidByte[i] < 0x10) uid += "0";
        uid += String(rfid.uid.uidByte[i], HEX);
        if (i < rfid.uid.size - 1) uid += ":";
    }
    uid.toUpperCase();
    client.publish(CLUSTER_TOPIC_RFID, uid.c_str());

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
}

// --- NUOVO ---
void riproduciAudio(const char* percorso) {
    if (audioInRiproduzione) return;

    audioSource = new AudioFileSourceSPIFFS(percorso);
    audioOutput = new AudioOutputI2S(0, AudioOutputI2S::INTERNAL_DAC);
    audioOutput->SetOutputModeMono(true);
    audioOutput->SetGain(0.5);
    audioWav = new AudioGeneratorWAV();
    audioWav->begin(audioSource, audioOutput);
    audioInRiproduzione = true;
}

// --- NUOVO ---
void gestisciAudio() {
    if (!audioInRiproduzione || audioWav == nullptr) return;

    if (audioWav->isRunning()) {
        audioWav->loop();
    }
    else {
        audioWav->stop();
        delete audioWav;    audioWav    = nullptr;
        delete audioSource; audioSource = nullptr;
        delete audioOutput; audioOutput = nullptr;
        audioInRiproduzione = false;
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
        else if (subStatoAttuale == 2) {
            riproduciAudio("/wait.wav");
        }
        // else if (substatoAttuale == 3) {
        // codice buzzer "passa"
        // }
    }

    statoPrecedentePulsante = stato;
}

void gestisciDati() {
    JsonDocument payloadJson;

    float umidita = dht.readHumidity();
    float temperatura = dht.readTemperature();
    String traffico;
    int veicoliTotali = 0;
    for (int i = 0; i < 10; i++) {
        if (veicoli[i]) {
            veicoliTotali++;
        }
    }
    if (veicoliTotali >= VEICOLI_TRAFFICO_ALTO) {
        traffico = "alto";
    }
    else if (veicoliTotali >= VEICOLI_TRAFFICO_MEDIO) {
        traffico = "medio";
    }
    else {
        traffico = "basso";
    }
    bool luminosita = analogRead(PIN_LUMINOSITA) < 1;
    bool acqua = analogRead(PIN_ACQUA) > SOGLIA_ACQUA;

    mpu.update();
    JsonObject giroscopio = payloadJson["giroscopio"].to<JsonObject>();
    giroscopio["x"] = mpu.getGyroX();
    giroscopio["y"] = mpu.getGyroY();
    giroscopio["z"] = mpu.getGyroZ();
    JsonObject accelerometro = payloadJson["accelerometro"].to<JsonObject>();
    accelerometro["x"] = mpu.getAccX();
    accelerometro["y"] = mpu.getAccY();
    accelerometro["z"] = mpu.getAccZ();
    payloadJson["traffico"]    = traffico;
    payloadJson["umidita"]     = umidita;
    payloadJson["temperatura"] = temperatura;
    payloadJson["luminosita"]  = luminosita;
    payloadJson["acqua"]       = acqua;

    String payloadString;
    serializeJson(payloadJson, payloadString);
    client.publish(CLUSTER_TOPIC_DATI, payloadString.c_str());
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
    Wire.begin();

    pinMode(PIN_LED_VERDE, OUTPUT);
    pinMode(PIN_LED_GIALLO, OUTPUT);
    pinMode(PIN_LED_ROSSO, OUTPUT);

    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT);

    pinMode(PIN_PULSANTE, INPUT_PULLUP);

    dht.begin();

    mpu.setup(0x68);

    SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_SS);
    rfid.PCD_Init();

    pinMode(PIN_LUMINOSITA, INPUT);
    pinMode(PIN_ACQUA, INPUT);

    SPIFFS.begin(true);
    // --- FINE NUOVO ---

    setup_wifi();

    espClient.setInsecure();
    client.setServer(CLUSTER_URL, CLUSTER_PORT);
    client.setCallback(callback);
}

void loop() {
    millisAttuali = millis();

    gestisciLed();
    gestisciHCSR04();
    gestisciPulsante();
    gestisciRFID();   // --- NUOVO ---
    gestisciAudio();  // --- NUOVO ---

    if (millisAttuali - millisPrecedentiDati >= DURATA_DATI) {
        gestisciDati();

        millisPrecedentiDati = millisAttuali;
    }

    if (!client.connected()) {
        reconnect();
    }

    client.loop();
}