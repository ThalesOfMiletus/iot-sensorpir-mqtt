#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ======== LED de estado da luz ========
const int LED_LUZ_PIN = 19;     // use 25/27/32/33 para LED externo
const bool LED_ATIVO_ALTO = true; // true: HIGH acende | false: LOW acende

// ----- LUZ (BH1750) -----
#include <Wire.h>
#include <BH1750.h>

BH1750 lightMeter(0x23);             // 0x23 (ADDR em GND/desconectado) | 0x5C (ADDR em 3V3)
const char* mqtt_topic_lux     = "grupothales/lux";
const char* mqtt_topic_luzflag = "grupothales/luz";   // ON/OFF

unsigned long lastLuxMs = 0;
const unsigned long LUX_INTERVAL_MS = 2000; // lê a cada 2s

// Histerese simples (ajuste conforme o ambiente)
const float LUX_ON  = 60.0;  // acima disso: consideramos “luz ligada”
const float LUX_OFF = 40.0;  // abaixo disso: consideramos “luz desligada”
bool luzLigada = false;

// ======== SENSOR ========
const int PIR_PIN = 13;
int lastState = LOW;
bool sensorEnabled = true; // controlado pelo Flask

// ======== BUZZER (ajuste aqui) ========
const int  BUZZER_PIN = 26;

// Se seu buzzer precisa de PWM para tocar (passivo) → true.
// Se ele toca sozinho com nível lógico (ativo) → false.
const bool BUZZER_PASSIVO = true;

// Só usado para buzzer ATIVO:
// true  = buzzer toca quando pino está HIGH (GPIO → + lado do buzzer)
// false = buzzer toca quando pino está LOW  (GPIO → lado GND do buzzer)
const bool BUZZER_ATIVO_NIVEL_ALTO = false;

// PWM para buzzer PASSIVO (volume baixo)
const int  BUZZER_CH   = 0;
const int  BUZZER_BITS = 10;
const int  BUZZER_DUTY_SOFT = 60;   // ~6% duty

// ======== CONFIG Wi-Fi ========
const char* ssid     = "AMF-CORP";
const char* password = "@MF$4515";

// ======== CONFIG MQTT ========
const char* mqtt_server = "test.mosquitto.org";
const int   mqtt_port   = 1883;
const char* mqtt_topic  = "grupothales/teste";

// ======== CONFIG FLASK (API externa) ========
const char* FLASK_BASE = "http://192.168.64.91:5000"; // << TROQUE pelo IP do PC com Flask

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

// ======== BUZZER: liga/desliga com segurança ========
// Desliga SEMPRE: alta impedância (evita afundar/fornecer corrente por engano)
void buzzer_off() {
  if (BUZZER_PASSIVO) {
    ledcWrite(BUZZER_CH, 0);
    ledcWriteTone(BUZZER_CH, 0);
    ledcDetachPin(BUZZER_PIN);
  }
  pinMode(BUZZER_PIN, INPUT); // alta impedância = mudo em qualquer fiação
}

// Bipe curto e baixo
void buzzer_beep(int freq_hz = 3000, int dur_ms = 120) {
  if (BUZZER_PASSIVO) {
    ledcSetup(BUZZER_CH, freq_hz, BUZZER_BITS);
    ledcAttachPin(BUZZER_PIN, BUZZER_CH);
    ledcWrite(BUZZER_CH, BUZZER_DUTY_SOFT);
    delay(dur_ms);
    buzzer_off();
  } else {
    pinMode(BUZZER_PIN, OUTPUT);
    int nivelAtivo   = BUZZER_ATIVO_NIVEL_ALTO ? HIGH : LOW;
    digitalWrite(BUZZER_PIN, nivelAtivo);
    delay(dur_ms);
    pinMode(BUZZER_PIN, INPUT); // alta-Z para silêncio total
  }
}

// ======== HTTP → Flask ========
bool http_post_event(const char* type, const char* detail) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = String(FLASK_BASE) + "/api/events";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["device_id"] = WiFi.macAddress();
  doc["type"] = type;
  if (detail) doc["detail"] = detail;
  // Se quiser enviar timestamp do ESP: doc["ts"] = "2025-09-19T23:59:00Z";

  String payload; serializeJson(doc, payload);
  int code = http.POST(payload);
  http.end();
  Serial.printf("[HTTP] POST /api/events (%d)\n", code);
  return code >= 200 && code < 300;
}

bool fetch_sensor_enabled(bool &outEnabled) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = String(FLASK_BASE) + "/api/sensor-state";
  http.begin(url);
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, http.getString());
    if (!err && doc.containsKey("enabled")) {
      outEnabled = doc["enabled"];
      ok = true;
    }
  }
  http.end();
  Serial.printf("[HTTP] GET /api/sensor-state (%d) -> %s\n", code, ok ? "OK" : "FAIL");
  return ok;
}

void setup() {
  pinMode(LED_LUZ_PIN, OUTPUT);
  digitalWrite(LED_LUZ_PIN, LED_ATIVO_ALTO ? LOW : HIGH); // começa apagado
  Wire.begin(21, 22);
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("[BH1750] OK");
  } else {
    Serial.println("[BH1750] ERRO ao iniciar (confira ligacoes/endereco)");
  }
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);

  // Buzzer inicia mudo
  buzzer_off();

  delay(1500);
  Serial.println("Iniciando sensor de presenca...");

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);

  // sincroniza estado inicial com o Flask
  bool en;
  if (fetch_sensor_enabled(en)) {
    sensorEnabled = en;
  }
  Serial.printf("[SENSOR] Estado inicial: %s\n", sensorEnabled ? "ATIVO" : "INATIVO");
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
  
// ======== LUZ: ler BH1750 e decidir se ligou/desligou ========
if (millis() - lastLuxMs >= LUX_INTERVAL_MS) {
  lastLuxMs = millis();
  float lux = lightMeter.readLightLevel();

  if (lux < 0 || isnan(lux)) {
    Serial.println("[BH1750] leitura invalida");
  } else {
    // Log e publish do valor
    char buf[32]; snprintf(buf, sizeof(buf), "%.2f", lux);
    Serial.printf("[BH1750] %.2f lux\n", lux);
    client.publish(mqtt_topic_lux, buf, true); // retain opcional

    // IF simples com histerese mínima:
    // Quando ligar a luz:
    if (!luzLigada && lux >= LUX_ON) {
      luzLigada = true;
      Serial.println("[LUZ] Ligada (detecao por lux)");
      client.publish(mqtt_topic_luzflag, "ON", true);
      http_post_event("light", "ON"); // se estiver usando Flask
      digitalWrite(LED_LUZ_PIN, LED_ATIVO_ALTO ? HIGH : LOW);   // ACENDE LED
    }
    // Quando desligar a luz:
    else if (luzLigada && lux <= LUX_OFF) {
      luzLigada = false;
      Serial.println("[LUZ] Desligada (detecao por lux)");
      client.publish(mqtt_topic_luzflag, "OFF", true);
      http_post_event("light", "OFF");
      digitalWrite(LED_LUZ_PIN, LED_ATIVO_ALTO ? LOW : HIGH);   // APAGA LED
    }
  }
}

  // puxa estado do sensor do Flask a cada 3s
  static uint32_t lastFetch = 0;
  if (millis() - lastFetch > 3000) {
    lastFetch = millis();
    bool en;
    if (fetch_sensor_enabled(en)) {
      if (en != sensorEnabled) {
        sensorEnabled = en;
        Serial.printf("[SENSOR] Novo estado: %s\n", sensorEnabled ? "ATIVO" : "INATIVO");
      }
    }
  }

  // Borda de subida -> bip + publish + enviar para Flask
  if (sensorEnabled) {
    int s = digitalRead(PIR_PIN);
    if (s != lastState) {
      lastState = s;
      if (s == HIGH) {
        Serial.println("[DETECCAO] Movimento!");
        buzzer_beep(3000, 120);  // bip único e baixo
        bool okM = client.publish(mqtt_topic, "DETECCAO Movimento!");
        Serial.printf("[MQTT] Publish '%s' → %s\n", mqtt_topic, okM ? "OK" : "FALHOU");
        http_post_event("motion", "detected"); // grava no Flask
      } else {
        Serial.println("[OK] Sem movimento.");
        buzzer_off();            // silêncio absoluto
        // Se quiser registrar "idle" no Flask, descomente:
        // http_post_event("motion", "idle");
      }
    }
  }

  delay(50);
}
