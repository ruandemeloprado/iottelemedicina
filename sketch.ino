#include <WiFi.h>
#include <PubSubClient.h>
#include "DHTesp.h"

// Sensor DHT22
const int DHT_PIN = 15;
DHTesp dhtSensor;

// WiFi - Wokwi tem uma configuração específica
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// MQTT HiveMQ
const char* mqtt_server = "broker.hivemq.com";

// MQTT topics
const char* topic_febre = "ruan/febre";
const char* topic_temp  = "ruan/temperatura";
const char* topic_estado = "ruan/estado";

// Limite de temperatura (febre)
const float TEMP_LIMITE = 37.5;

// LED
const int LED_PIN = 2;

// Objetos MQTT
WiFiClient espClient;
PubSubClient client(espClient);

// Controle de tempo
unsigned long lastMsg = 0;
bool sistemaPronto = false;

void setup_wifi() {
  Serial.println();
  Serial.println("=== CONFIGURAÇÃO WiFi ===");
  Serial.print("Conectando à rede: ");
  Serial.println(ssid);
  
  // Método mais simples e confiável para Wokwi
  WiFi.mode(WIFI_STA);
  
  // Limpar configurações anteriores
  WiFi.disconnect(true);
  delay(1000);
  
  // Iniciar conexão
  WiFi.begin(ssid, password);
  
  Serial.println("Iniciando conexão WiFi...");
  
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 25) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi CONECTADO!");
    Serial.print("📡 IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("📶 Força do sinal: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    sistemaPronto = true;
  } else {
    Serial.println("\n❌ FALHA na conexão WiFi após " + String(tentativas) + " tentativas!");
    Serial.println("Status: " + String(WiFi.status()));
    sistemaPronto = false;
    
    // Tentar uma última vez de forma diferente
    Serial.println("Tentando conexão alternativa...");
    WiFi.disconnect(true);
    delay(2000);
    WiFi.begin(ssid, password);
    delay(5000);
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("✅ Conexão alternativa funcionou!");
      sistemaPronto = true;
    }
  }
  Serial.println("==========================");
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("📨 Mensagem recebida [");
  Serial.print(topic);
  Serial.print("]: ");
  
  String mensagem;
  for (int i = 0; i < length; i++) {
    mensagem += (char)payload[i];
  }
  Serial.println(mensagem);
}

void reconnectMQTT() {
  if (!sistemaPronto) return;
  
  Serial.print("🔗 Tentando conectar ao MQTT... ");
  
  String clientId = "ESP32-Febre-" + String(random(0xffff), HEX);
  
  if (client.connect(clientId.c_str())) {
    Serial.println("✅ CONECTADO!");
    client.subscribe(topic_febre);
    Serial.println("📝 Inscrito no tópico: " + String(topic_febre));
  } else {
    Serial.print("❌ Falha, rc=");
    Serial.print(client.state());
    Serial.println(" - Tentando novamente em 5s");
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000); // Delay maior para estabilização
  
  Serial.println("🔄 Iniciando Sistema de Detecção de Febre...");
  Serial.println("===========================================");
  
  // Configurar LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Serial.println("💡 LED configurado no pino 2");

  // Inicializar sensor DHT
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
  delay(3000);
  Serial.println("🌡️ Sensor DHT22 inicializado no pino 15");
  
  // Teste rápido do sensor
  TempAndHumidity teste = dhtSensor.getTempAndHumidity();
  if (!isnan(teste.temperature)) {
    Serial.println("✅ Sensor DHT22 funcionando corretamente");
  } else {
    Serial.println("⚠️ Sensor DHT22 pode ter problemas");
  }

  // Conectar WiFi
  setup_wifi();

  // Configurar MQTT se WiFi estiver ok
  if (sistemaPronto) {
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
    client.setKeepAlive(90);
    Serial.println("📡 Cliente MQTT configurado");
    
    // Tentar primeira conexão MQTT
    reconnectMQTT();
  }
  
  Serial.println("✅ Sistema inicializado!");
  Serial.println("===========================================");
}

void loop() {
  // Verificar WiFi a cada 30 segundos se estiver desconectado
  static unsigned long lastWifiCheck = 0;
  if (!sistemaPronto && millis() - lastWifiCheck > 30000) {
    Serial.println("🔄 Verificando conexão WiFi...");
    setup_wifi();
    lastWifiCheck = millis();
  }
  
  // Manter conexão MQTT
  if (sistemaPronto && !client.connected()) {
    reconnectMQTT();
  }
  
  if (sistemaPronto) {
    client.loop();
  }

  // Leitura dos sensores a cada 5 segundos
  if (millis() - lastMsg > 5000) {
    lastMsg = millis();
    
    if (!sistemaPronto) {
      Serial.println("⏳ Aguardando conexão WiFi...");
      return;
    }

    // Ler sensor DHT
    TempAndHumidity data = dhtSensor.getTempAndHumidity();
    
    if (isnan(data.temperature) || isnan(data.humidity)) {
      Serial.println("❌ Erro na leitura do DHT22!");
      
      // Tentar reinicializar o sensor
      dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
      delay(1000);
      return;
    }

    float temperatura = data.temperature;
    float umidade = data.humidity;

    // Exibir dados
    Serial.println("\n📊 === DADOS DO SENSOR ===");
    Serial.print("🌡️ Temperatura: ");
    Serial.print(temperatura, 1);
    Serial.println(" °C");
    Serial.println(" %");

    // Verificar febre
    bool estado_febril = (temperatura >= TEMP_LIMITE);
    
    if (estado_febril) {
      digitalWrite(LED_PIN, HIGH);
      Serial.println("🚨 *** FEBRE DETECTADA! ***");
    } else {
      digitalWrite(LED_PIN, LOW);
      Serial.println("✅ Estado normal");
    }

    // Enviar dados via MQTT
    if (client.connected()) {
      String msgTemperatura = String(temperatura, 2);
      String msgEstado = String(estado_febril ? 1 : 0);
      
      bool envio1 = client.publish(topic_temp, msgTemperatura.c_str());
      bool envio2 = client.publish(topic_estado, msgEstado.c_str());
      bool envio3 = client.publish(topic_febre, msgEstado.c_str());
      
      if (envio1 && envio2 && envio3) {
        Serial.println("📤 Dados enviados para MQTT:");
        Serial.println("   Febre: " + msgEstado);
      } else {
        Serial.println("❌ Falha no envio MQTT");
      }
    } else {
      Serial.println("⚠️ MQTT desconectado - dados não enviados");
    }
    
    Serial.println("=================================");
  }
}