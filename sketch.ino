#include <WiFi.h>
#include <PubSubClient.h>
#include "DHTesp.h"

// Sensor DHT22
const int DHT_PIN = 15;
DHTesp dhtSensor;

// WiFi - Wokwi
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// MQTT HiveMQ
const char* mqtt_server = "broker.hivemq.com";

// Topics
const char* topic_febre = "ruan/febre";
const char* topic_temp  = "ruan/temperatura";
const char* topic_estado = "ruan/estado";

// LED
const int LED_PIN = 2;

// Controle
WiFiClient espClient;
PubSubClient client(espClient);

// Controle de tempo
unsigned long lastMsg = 0;
bool sistemaPronto = false;

// ========= NOVO: TEMPO DO ENVIO DE COMANDO =========
unsigned long t_envio_comando = 0;


// ===================================================
//  WiFi
// ===================================================
void setup_wifi() {
  Serial.println("\n=== CONFIG WiFi ===");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(800);

  WiFi.begin(ssid, password);
  Serial.println("Conectando...");

  int tent = 0;
  while (WiFi.status() != WL_CONNECTED && tent < 25) {
    delay(400);
    Serial.print(".");
    tent++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK!");
    sistemaPronto = true;
  } else {
    Serial.println("\nFalha no WiFi");
    sistemaPronto = false;
  }
}


// ===================================================
//  CALLBACK MQTT
// ===================================================
void callback(char* topic, byte* payload, unsigned int length) 
{
  unsigned long t_recebida = micros();

  Serial.print("\n📨 Mensagem recebida [");
  Serial.print(topic);
  Serial.print("]: ");

  String msg = "";
  for (int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println(msg);

  // Ação do LED
  if (msg == "1")
    digitalWrite(LED_PIN, HIGH);
  else
    digitalWrite(LED_PIN, LOW);

  // ======== Tempo comando → LED ========
  float t_atuador_ms = (micros() - t_recebida) / 1000.0;
  Serial.print("⏱️ Tempo comando MQTT → LED: ");
  Serial.print(t_atuador_ms);
  Serial.println(" ms");

  // ======== Tempo envio comando → retorno ========
  if (t_envio_comando > 0) {
    float t_retorno_ms = (t_recebida - t_envio_comando) / 1000.0;
    Serial.print("📡 Tempo envio comando MQTT → recebimento no ESP32: ");
    Serial.print(t_retorno_ms);
    Serial.println(" ms");

    t_envio_comando = 0;  // limpa
  }
}


// ===================================================
//  Reconectar MQTT
// ===================================================
void reconnectMQTT() {
  if (!sistemaPronto) return;

  Serial.print("Conectando ao MQTT...");
  if (client.connect("ESP32-Febre")) {
    Serial.println("OK!");
    client.subscribe(topic_febre);
  } else {
    Serial.println("FALHA");
  }
}


// ===================================================
//  SETUP
// ===================================================
void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(LED_PIN, OUTPUT);
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);

  setup_wifi();

  if (sistemaPronto) {
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
    reconnectMQTT();
  }

  Serial.println("Sistema iniciado.\n");
}


// ===================================================
//  LOOP PRINCIPAL
// ===================================================
void loop() {
  if (sistemaPronto && !client.connected()) reconnectMQTT();
  if (sistemaPronto) client.loop();

  if (millis() - lastMsg > 5000) {
    lastMsg = millis();

    // ======== Leitura do sensor ========
    unsigned long t_sensor_lido = micros();
    TempAndHumidity d = dhtSensor.getTempAndHumidity();

    if (isnan(d.temperature)) {
      Serial.println("Sensor falhou!");
      return;
    }

    float temperatura = d.temperature;
    bool febre = temperatura >= 37.5;

    // LED local
    digitalWrite(LED_PIN, febre ? HIGH : LOW);

    // ======== Envio MQTT ========
    if (client.connected()) {

      client.publish(topic_temp, String(temperatura).c_str());
      client.publish(topic_estado, febre ? "1" : "0");

      // MARCA TEMPO DO COMANDO ENVIADO
      t_envio_comando = micros();
      client.publish(topic_febre, febre ? "1" : "0");

      // ======== Sensor → MQTT ========
      float t_sensor_ms = (micros() - t_sensor_lido) / 1000.0;

      Serial.println("\n📊 SENSOR → MQTT:");
      Serial.print("⏱️ Tempo leitura → envio MQTT: ");
      Serial.print(t_sensor_ms);
      Serial.println(" ms");
      Serial.println("=============================");
    }
  }
}
