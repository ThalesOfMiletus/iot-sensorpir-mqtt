# ESP32 PIR → MQTT (PlatformIO)

Detecta movimento com um **sensor PIR** no **ESP32** e publica eventos em um **broker MQTT**. Projeto em **PlatformIO (Arduino framework)** com **Wi-Fi + PubSubClient**.

## ✨ Funcionalidades

* Leitura de um sensor PIR no **GPIO 13**.
* Conexão Wi-Fi e **reconexão automática** ao MQTT.
* Publicação em tópico MQTT na detecção de movimento.
* Logs detalhados no terminal (Serial).

## 🧱 Hardware

* ESP32 DevKit (ou equivalente)
* Sensor PIR (ex.: HC-SR501 ou AM312)
* Jumpers

### Ligações (exemplo)

| PIR | ESP32                        |
| --- | ---------------------------- |
| VCC | 5V (ou 3V3, conforme modelo) |
| GND | GND                          |
| OUT | **GPIO 13**                  |

> Obs.: Se o **OUT** do seu PIR for 5V, utilize um **divisor resistivo** para o GPIO do ESP32 (que é 3,3V).

## 🗂 Estrutura

```
.
├─ src/
│  └─ main.cpp        # código principal (Wi-Fi + MQTT + PIR no GPIO 13)
├─ platformio.ini     # config do projeto e libs
└─ README.md
```

## 🔧 Pré-requisitos

* **VS Code** + extensão **PlatformIO IDE**
* Broker MQTT (ex.: `test.mosquitto.org`) para testes
* (Opcional) Cliente MQTT no PC: `mosquitto_sub` / `mosquitto_pub`

## ⚙️ Configuração

`platformio.ini` sugerido:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

monitor_speed = 115200

lib_deps = 
  knolleary/PubSubClient@^2.8
```

No `src/main.cpp`, ajuste:

```cpp
// Wi-Fi
const char* ssid     = "AMF-CORP";
const char* password = "@MF$4515";

// MQTT
const char* mqtt_server = "test.mosquitto.org";
const int   mqtt_port   = 1883;
const char* mqtt_topic  = "grupothales/teste";
```

## ▶️ Como rodar

1. **Clone** este repositório e abra a pasta no VS Code.
2. Abra o PlatformIO → **Build** (ou `pio run`) para compilar.
3. **Upload** para a placa (ou `pio run -t upload`).

   > Se necessário, defina a porta:
   > `upload_port = COM5` (Windows) ou `/dev/ttyUSB0` (Linux) no `platformio.ini`.
4. **Monitor serial**: `pio device monitor -b 115200`.
5. **Assine o tópico** no PC para ver as mensagens:

   ```bash
   mosquitto_sub -h test.mosquitto.org -t grupothales/teste -v
   ```
6. Mova-se na frente do PIR. A cada detecção, o ESP32 publica algo como:

   ```
   grupothales/teste DETECCAO Movimento!
   ```

## 🛰️ Payloads e tópicos

* **Tópico:** `grupothales/teste`
* **Mensagens:**

  * `"HELLO (boot/reconnect)"` (enviada na conexão ao broker, se habilitado no código de debug)
  * `"DETECCAO Movimento!"` (enviada na borda de subida do PIR — quando detecta)

## 🧪 Log esperado (Serial)

Exemplo de inicialização e publish:

```
[WiFi] Conectando-se à rede...
................................
[WiFi] Conectado!
[WiFi] IP: 192.168.0.123
[MQTT] Tentando conectar em test.mosquitto.org:1883 (tentativa #1)...
[MQTT] Conectado! clientId=ESP32Client-<hex>
Iniciando sensor de presenca...
[DETECCAO] Movimento!
[MQTT] Publish 'grupothales/teste' → OK
```

## 🧯 Solução de problemas

* **Porta não encontrada**: `pio device list` e fixe `upload_port`/`monitor_port` no `platformio.ini`.
* **“Failed to connect to ESP32: Timed out”**: mantenha **BOOT** pressionado no início do upload; feche o Monitor Serial; reduza `upload_speed`.
* **Disparos “infinitos” do PIR**:

  * Espere **30–60 s** após energizar (aquecimento).
  * Ajuste trimpots (**SENS** ↓ e **TIME** \~2–3 s).
  * Evite **GPIOs de boot** (0, 2, 12, 15). Use 13/27/32/33.
  * `pinMode(PIR_PIN, INPUT_PULLDOWN)` pode ajudar (dependendo do sensor).
  * Desacople VCC do PIR (100 nF + 10 µF com GND).
* **Broker público instável**: `test.mosquitto.org` pode cair. Alternativas:

  * `broker.hivemq.com`, `mqtt.eclipseprojects.io`, ou um broker local (Mosquitto).

## 🔒 Segurança

* Brokers públicos **não têm** autenticação/TLS por padrão. Para produção:

  * Use **broker próprio** (Mosquitto) com **usuário/senha** e/ou **TLS**.
  * Gere **clientId** único (já há exemplo no código).
  * Evite publicar dados sensíveis.

## 🛠 Ideias de evolução

* Publicar também **“sem movimento”** (LOW) ou enviar **JSON** (`{"motion":true,"ts":...}`).
* **Cooldown** para reduzir spam de mensagens.
* **OTA** (atualização de firmware pela rede).
* Integrar com **Home Assistant** / **Node-RED**.

## 🤝 Contribuição

Sinta-se à vontade para abrir **Issues** e **PRs**:

* Melhoria de documentação
* Suporte a outros sensores/placas
* Exemplos de integração com automação residencial
