#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ======== SENSOR ========
const int PIR_PIN = 13;
int lastState = LOW;

// ======== BUZZER (baixo volume) ========
const int  BUZZER_PIN = 26;
const bool BUZZER_PASSIVO = true;  // true = passivo (PWM), false = ativo (liga/desliga)
const int  BUZZER_CH   = 0;        // canal PWM 0..15
const int  BUZZER_BITS = 10;       // resolução PWM (0..1023)
const int  BUZZER_DUTY_SOFT = 30;  // ~6% de duty => som mais baixo

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

// ======== Wi-Fi ========
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

// ======== MQTT ========
void reconnect() {
  static unsigned long tentativas = 0;
  while (!client.connected()) {
    tentativas++;
    Serial.printf("[MQTT] Tentando conectar em %s:%d (tentativa #%lu)...\n",
                  mqtt_server, mqtt_port, tentativas);

    // clientId único evita conflito no broker público
    String clientId = "ESP32Client-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.printf("[MQTT] Conectado! clientId=%s\n", clientId.c_str());
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

// ======== BUZZER helpers ========
void buzzer_silence() {
  if (BUZZER_PASSIVO) {
    ledcWrite(BUZZER_CH, 0);  // duty 0 = sem som
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// beep suave/baixo
void buzzer_beep(int freq_hz = 1500, int dur_ms = 120) {
  if (BUZZER_PASSIVO) {
    // define freq e aplica duty baixo (volume reduzido)
    ledcWriteTone(BUZZER_CH, freq_hz);
    ledcWrite(BUZZER_CH, BUZZER_DUTY_SOFT);
    delay(dur_ms);
    ledcWrite(BUZZER_CH, 0); // silencia
  } else {
    // ativo: simula volume menor com pulsos (20% duty ~100 Hz)
    uint32_t t0 = millis();
    const int ON_MS = 2, OFF_MS = 8;
    while (millis() - t0 < (uint32_t)dur_ms) {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(ON_MS);
      digitalWrite(BUZZER_PIN, LOW);
      delay(OFF_MS);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);

  // BUZZER setup
  pinMode(BUZZER_PIN, OUTPUT);
  if (BUZZER_PASSIVO) {
    // configura PWM; a frequência é ajustada em runtime via ledcWriteTone
    ledcSetup(BUZZER_CH, 2000, BUZZER_BITS);
    ledcAttachPin(BUZZER_PIN, BUZZER_CH);
    ledcWrite(BUZZER_CH, 0); // começa em silêncio
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }

  delay(1500);
  Serial.println("[INFO] Iniciando sensor de presenca...");

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  // Reconnect Wi-Fi se cair
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Desconectado. Re-conectando...");
    setup_wifi();
  }

  // Garante conexão MQTT
  if (!client.connected()) reconnect();
  client.loop();

  // Publica somente na borda de subida (detecção) e toca buzzer baixo
  int s = digitalRead(PIR_PIN);
  if (s != lastState) {
    lastState = s;
    if (s == HIGH) {
      Serial.println("[DETECCAO] Movimento!");
      buzzer_beep(1500, 120);  // volume baixo
      bool ok = client.publish(mqtt_topic, "DETECCAO Movimento!");
      Serial.printf("[MQTT] Publish '%s' → %s\n", mqtt_topic, ok ? "OK" : "FALHOU");
    } else {
      Serial.println("[OK] Sem movimento.");
      buzzer_silence();
    }
  }

  delay(50);
}
