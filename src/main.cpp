#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ======== SENSOR ========
const int PIR_PIN = 13;
int lastState = LOW;

// ======== CONFIG Wi-Fi ========
const char* ssid     = "AMF-CORP";
const char* password = "@MF$4515";

// ======== CONFIG MQTT ========
const char* mqtt_server = "test.mosquitto.org";
const int   mqtt_port   = 1883;
const char* mqtt_topic  = "grupothales/teste";

WiFiClient espClient;
PubSubClient client(espClient);

// ---- helper p/ imprimir o estado do MQTT
const char* mqttStateStr(int s) {
  switch (s) {
    case -4: return "MQTT_CONNECTION_TIMEOUT";
    case -3: return "MQTT_CONNECTION_LOST";
    case -2: return "MQTT_CONNECT_FAILED";
    case -1: return "MQTT_DISCONNECTED";
    case  0: return "MQTT_CONNECTED";
    case  1: return "MQTT_CONNECT_BAD_PROTOCOL";
    case  2: return "MQTT_CONNECT_BAD_CLIENT_ID";
    case  3: return "MQTT_CONNECT_UNAVAILABLE";
    case  4: return "MQTT_CONNECT_BAD_CREDENTIALS";
    case  5: return "MQTT_CONNECT_UNAUTHORIZED";
    default: return "MQTT_UNKNOWN";
  }
}

// ======== Conectar Wi-Fi ========
void setup_wifi() {
  Serial.println("[WiFi] Conectando-se à rede...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\n[WiFi] Conectado!");
  Serial.print("[WiFi] IP: "); Serial.println(WiFi.localIP());
}

// ======== Reconectar ao broker ========
void reconnect() {
  static unsigned long tentativas = 0;
  while (!client.connected()) {
    tentativas++;
    Serial.printf("[MQTT] Tentando conectar em %s:%d (tentativa #%lu)...\n",
                  mqtt_server, mqtt_port, tentativas);

    // clientId único ajuda a evitar conflito no broker público
    String clientId = "ESP32Client-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.printf("[MQTT] Conectado! clientId=%s\n", clientId.c_str());
      // mensagem de hello na (re)conexão
      bool ok = client.publish(mqtt_topic, "HELLO (boot/reconnect)");
      Serial.printf("[MQTT] Publish HELLO → %s\n", ok ? "OK" : "FALHOU");
    } else {
      int st = client.state();
      Serial.printf("[MQTT] Falha. state=%d (%s). Aguardando 2s...\n",
                    st, mqttStateStr(st));
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  delay(1500);
  Serial.println("Iniciando sensor de presenca...");

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  // Se cair o Wi-Fi, avisa e reconecta
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Desconectado. Re-conectando...");
    setup_wifi();
  }

  // Garante conexão MQTT
  if (!client.connected()) reconnect();
  client.loop();

  // Publica somente na borda de subida (detecção)
  int s = digitalRead(PIR_PIN);
  if (s != lastState) {
    lastState = s;
    if (s == HIGH) {
      Serial.println("[DETECCAO] Movimento!");
      bool ok = client.publish(mqtt_topic, "DETECCAO Movimento!");
      Serial.printf("[MQTT] Publish '%s' → %s\n", mqtt_topic, ok ? "OK" : "FALHOU");
    } else {
      // log opcional do retorno ao repouso:
      Serial.println("[OK] Sem movimento.");
    }
  }

  delay(50);
}
