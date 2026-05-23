#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

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

// impostazioni pulsane
const int PIN_PULSANTE = 15;
bool statoPrecedentePulsante = HIGH;

// impostazioni DHT22
const int PIN_DHT = 18;
DHT dht(PIN_DHT, DHT22);

// impostazioni wifi
const char* WIFI_SSID = "Tibaldo";
const char* WIFI_PASSWORD = "ciao1234";
WiFiClientSecure espClient;

// impostazioni cluster
const char* CLUSTER_URL = "bf89bd34a48e407abd232255e6194d47.s1.eu.hivemq.cloud";
const int CLUSTER_PORT = 8883;
const char* CLUSTER_USERNAME = "DevEsp";
const char* CLUSTER_PASSWORD = "DevEsp123";
const char* CLUSTER_TOPIC_COMANDI = "esp/comandi";
const char* CLUSTER_TOPIC_DATI = "esp/dati";
const char* CLUSTER_TOPIC_LUCE = "esp/luce";
PubSubClient client(espClient);

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
    if ((subStatoAttuale == 1 || subStatoAttuale == 2) && millisAttuali - millisPrecedentiHCSR04 >= DURATA_LETTURA_DISTANZA) {
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

void gestisciPulsante() {
    bool stato = digitalRead(PIN_PULSANTE);

    if (statoPrecedentePulsante == HIGH && stato == LOW) {
        String payload = "pulsante premuto";
        client.publish(CLUSTER_TOPIC_DATI, payload.c_str());
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

    payloadJson["traffico"] = traffico;
    payloadJson["umidita"] = umidita;
    payloadJson["temperatura"] = temperatura;

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
    pinMode(PIN_LED_VERDE, OUTPUT);
    pinMode(PIN_LED_GIALLO, OUTPUT);
    pinMode(PIN_LED_ROSSO, OUTPUT);

    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT);

    pinMode(PIN_PULSANTE, INPUT_PULLUP);

    dht.begin();

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

    if (millisAttuali - millisPrecedentiDati >= DURATA_DATI) {
        gestisciDati();

        millisPrecedentiDati = millisAttuali;
    }

    if (!client.connected()) {
        reconnect();
    }

    client.loop();
}