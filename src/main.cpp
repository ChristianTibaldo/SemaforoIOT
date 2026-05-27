#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
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

PubSubClient client(espClient);

// impostazioni giroscopio/accelerometro
Adafruit_MPU6050 mpu;

bool terremoto = false;
unsigned long millisPrecedentiMPU = 0;
const int DURATA_MPU = 100;

// impostazioni HW-072 (luminosità)
const int PIN_LUMINOSITA = 35;

// impostazioni sensore acqua
const int PIN_ACQUA = 34;
const int SOGLIA_ACQUA = 250;

// impostazioni RFID-RC522
const int PIN_RFID_SS = 27;
const int PIN_RFID_RST = 32;
const int PIN_RFID_SCK = 14;
const int PIN_RFID_MOSI = 13;
const int PIN_RFID_MISO = 33;

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

            else if (subStatoAttuale == 1 &&
                     millisAttuali - millisPrecedentiLED >= DURATA_VERDE) {

                digitalWrite(PIN_LED_VERDE, LOW);
                digitalWrite(PIN_LED_GIALLO, HIGH);

                subStatoAttuale = 2;
                millisPrecedentiLED = millisAttuali;

                payload = "giallo";
                client.publish(CLUSTER_TOPIC_LUCE, payload.c_str());
            }

            else if (subStatoAttuale == 2 &&
                     millisAttuali - millisPrecedentiLED >= DURATA_GIALLO) {

                digitalWrite(PIN_LED_GIALLO, LOW);
                digitalWrite(PIN_LED_ROSSO, HIGH);

                subStatoAttuale = 3;
                millisPrecedentiLED = millisAttuali;

                payload = "rosso";
                client.publish(CLUSTER_TOPIC_LUCE, payload.c_str());
            }

            else if (subStatoAttuale == 3 &&
                     millisAttuali - millisPrecedentiLED >= DURATA_ROSSO) {

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
                } else {
                    payload = "giallo";
                }

                client.publish(CLUSTER_TOPIC_LUCE, payload.c_str());
            }

            subStatoAttuale = 0;

            break;
    }
}

void gestisciHCSR04() {

    if (subStatoAttuale != 3 &&
        millisAttuali - millisPrecedentiHCSR04 >= DURATA_LETTURA_DISTANZA) {

        digitalWrite(PIN_TRIG, LOW);
        delayMicroseconds(2);

        digitalWrite(PIN_TRIG, HIGH);
        delayMicroseconds(10);

        digitalWrite(PIN_TRIG, LOW);

        long durata = pulseIn(PIN_ECHO, HIGH, 30000);

        float distanza = (durata * 0.0343) / 2.0;

        if (distanza > 0 && distanza < MASSIMA_DISTANZA) {
            veicoli[indiceVeicoli] = true;
        } else {
            veicoli[indiceVeicoli] = false;
        }

        indiceVeicoli = wrap(indiceVeicoli + 1, 0, 10);

        millisPrecedentiHCSR04 = millisAttuali;
    }
}

void gestisciMPU6050() {

    if (millisAttuali - millisPrecedentiMPU >= DURATA_MPU) {

        sensors_event_t a, g, temp;

        mpu.getEvent(&a, &g, &temp);

        float accTotale = sqrt(
            a.acceleration.x * a.acceleration.x +
            a.acceleration.y * a.acceleration.y +
            a.acceleration.z * a.acceleration.z
        );

        // differenza rispetto alla gravità terrestre
        float delta = abs(accTotale - 9.81);

        // soglia vera anti-rumore
        terremoto = delta > 3.0;

        millisPrecedentiMPU = millisAttuali;

        Serial.print("ACC: ");
        Serial.print(accTotale);

        Serial.print(" DELTA: ");
        Serial.print(delta);

        Serial.print(" TERREMOTO: ");
        Serial.println(terremoto);
    }
}

void gestisciRFID() {

    if (!rfid.PICC_IsNewCardPresent()) return;

    if (!rfid.PICC_ReadCardSerial()) return;

    String uid = "";

    for (byte i = 0; i < rfid.uid.size; i++) {

        if (rfid.uid.uidByte[i] < 0x10) {
            uid += "0";
        }

        uid += String(rfid.uid.uidByte[i], HEX);

        if (i < rfid.uid.size - 1) {
            uid += ":";
        }
    }

    uid.toUpperCase();

    client.publish(CLUSTER_TOPIC_RFID, uid.c_str());

    delay(200);

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
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

    payloadJson["traffico"] = traffico;
    payloadJson["umidita"] = umidita;
    payloadJson["temperatura"] = temperatura;
    payloadJson["luminosita"] = luminosita;
    payloadJson["acqua"] = acqua;
    payloadJson["terremoto"] = terremoto;

    String payloadString;

    serializeJson(payloadJson, payloadString);

    client.publish(CLUSTER_TOPIC_DATI, payloadString.c_str());
}

void callback(char* topic, byte* payload, unsigned int length) {

    char messaggio[length + 1];

    memcpy(messaggio, payload, length);

    messaggio[length] = '\0';

    if (strcmp(messaggio, "spento") == 0) {
        statoAttuale = SPENTO;
    }

    else if (strcmp(messaggio, "attivo") == 0) {
        statoAttuale = ATTIVO;
    }

    else if (strcmp(messaggio, "inattivo") == 0) {
        statoAttuale = INATTIVO;
    }
}

void reconnect() {

    while (!client.connected()) {

        String clientId = "ESP-Client-";
        clientId += String(random(0xffff), HEX);

        if (client.connect(
                clientId.c_str(),
                CLUSTER_USERNAME,
                CLUSTER_PASSWORD)) {

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

    if (!mpu.begin()) {

        Serial.println("Impossibile trovare il chip MPU6050!");

    } else {

        mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    }

    SPI.begin(
        PIN_RFID_SCK,
        PIN_RFID_MISO,
        PIN_RFID_MOSI,
        PIN_RFID_SS
    );

    rfid.PCD_Init();

    delay(50);

    rfid.PCD_SetAntennaGain(rfid.RxGain_max);

    pinMode(PIN_LUMINOSITA, INPUT);
    pinMode(PIN_ACQUA, INPUT);

    SPIFFS.begin(true);

    setup_wifi();

    espClient.setInsecure();

    client.setServer(CLUSTER_URL, CLUSTER_PORT);

    client.setCallback(callback);
}

void loop() {

    millisAttuali = millis();

    gestisciLed();

    gestisciHCSR04();

    gestisciMPU6050();

    gestisciPulsante();

    gestisciRFID();

    if (millisAttuali - millisPrecedentiDati >= DURATA_DATI) {

        gestisciDati();

        millisPrecedentiDati = millisAttuali;
    }

    if (!client.connected()) {
        reconnect();
    }

    client.loop();
}