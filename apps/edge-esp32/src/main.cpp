#include <Arduino.h>
#include <DHTesp.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// Credenciais e host via macros em config.h (não versionado)
// Crie src/config.h com seus dados a partir de config.h.example
// e NUNCA commit seu config.h.
// Ex.: #define WIFI_SSID "..."  #define WIFI_PASS "..."
//      #define MQTT_HOST "..."  #define MQTT_PORT 8883
//      #define MQTT_USER "..."  #define MQTT_PASS "..."


// --- Configurações de pinos ---
static const int PIN_DHT = 15;     // DHT22 no GPIO 15
static const int PIN_BTN = 4;      // Botão no GPIO 4 (pull-down externo)

// --- Janelas e tempos ---
static const uint32_t DHT_INTERVAL_MS = 2000;   // leitura a cada 2s
static const uint32_t BPM_WINDOW_MS  = 10000;  // janela de 10s

// --- WiFi/MQTT (via config.h) ---
#include "config.h"
#ifndef WIFI_SSID
#define WIFI_SSID "Wokwi-GUEST"   // default para Wokwi
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif
#ifndef MQTT_HOST
#define MQTT_HOST "185a5dc3e6434acf93f3c4da0ae7f24d.s1.eu.hivemq.cloud"
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 8883
#endif
#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASS
#define MQTT_PASS ""
#endif
static const char* MQTT_TOPIC = "cardioia/ana/v1/vitals";

// --- Estado global ---
DHTesp dht;
volatile uint32_t pulseCount = 0;  // contador de pulsos (batimentos)
volatile int lastBtnState = 0;     // para filtrar bouncing rápido dentro da ISR

bool CONNECTED = false;            // estado de conectividade

// Controle de tempo
uint32_t lastDhtRead = 0;
uint32_t windowStart = 0;

// Amostras recentes
float lastTemp = NAN;
float lastHum  = NAN;
int lastBpm    = 0;

// MQTT client (TLS)
WiFiClientSecure tlsClient;
PubSubClient mqtt(tlsClient);
unsigned long mqttBackoffMs = 1000;     // backoff inicial 1s
unsigned long mqttNextRetry = 0;        // millis para próxima tentativa
String mqttClientId;

// --- Fila em RAM (offline buffer) ---
static const size_t RAM_QUEUE_MAX = 200; // ~200 amostras (~33 minutos em janelas de 10s)
String ramQueue[RAM_QUEUE_MAX];
size_t ramHead = 0, ramTail = 0, ramCount = 0;

void ramEnqueue(const String& line) {
  if (ramCount == RAM_QUEUE_MAX) {
    // descarta a mais antiga (ring buffer)
    ramTail = (ramTail + 1) % RAM_QUEUE_MAX;
    ramCount--;
  }
  ramQueue[ramHead] = line;
  ramHead = (ramHead + 1) % RAM_QUEUE_MAX;
  ramCount++;
}

size_t ramFlushPublish() {
  size_t sent = 0;
  while (ramCount > 0 && mqtt.connected()) {
    String line = ramQueue[ramTail];
    ramTail = (ramTail + 1) % RAM_QUEUE_MAX;
    ramCount--;
    bool ok = mqtt.publish(MQTT_TOPIC, line.c_str());
    if (ok) {
      sent++;
    } else {
      // se falhar, interrompe para tentar depois
      // reposiciona ponteiro para tentar novamente no futuro
      ramTail = (ramTail + RAM_QUEUE_MAX - 1) % RAM_QUEUE_MAX;
      ramCount++;
      break;
    }
  }
  if (sent > 0) {
    Serial.print(F("RAM_FLUSH ")); Serial.println((unsigned long)sent);
  }
  return sent;
}

// --- ISR de pulso no botão (conta borda de subida) ---
void IRAM_ATTR onButtonChange() {
  int state = digitalRead(PIN_BTN);
  if (state == HIGH && lastBtnState == LOW) {
    pulseCount++;
  }
  lastBtnState = state;
}

// --- Leitura do DHT com proteção simples ---
void readDhtIfDue() {
  uint32_t now = millis();
  if (now - lastDhtRead >= DHT_INTERVAL_MS) {
    lastDhtRead = now;
    TempAndHumidity th = dht.getTempAndHumidity();
    if (!isnan(th.temperature) && !isnan(th.humidity)) {
      lastTemp = th.temperature;
      lastHum  = th.humidity;
      // Exibe leituras no Serial Monitor (Wokwi)
      Serial.print(F("TEMP(")); Serial.print(PIN_DHT); Serial.print(F(")= "));
      Serial.print(lastTemp, 2);
      Serial.print(F(" °C  HUM= "));
      Serial.print(lastHum, 2);
      Serial.println(F(" %"));
    } else {
      // Mantém últimos valores válidos
    }
  }
}

// --- Calcula BPM ao fim de janela de 10s ---
bool computeBpmIfWindowDone() {
  uint32_t now = millis();
  if (now - windowStart >= BPM_WINDOW_MS) {
    noInterrupts();
    uint32_t pulses = pulseCount;
    pulseCount = 0; // reinicia para próxima janela
    interrupts();

    lastBpm = (int)(pulses * 6); // 10s * 6 = 60s
    windowStart = now;
    return true;
  }
  return false;
}

// --- Monta JSON linha única ---
String makeSampleJson(uint32_t ts, float temp, float hum, int bpm, bool connected) {
  String s = "{";
  s += "\"ts\":"; s += ts;
  s += ",\"temp\":"; s += String(temp, 2);
  s += ",\"hum\":"; s += String(hum, 2);
  s += ",\"bpm\":"; s += bpm;
  s += ",\"connected\":"; s += (connected ? "true" : "false");
  s += "}";
  return s;
}

// --- WiFi/MQTT helpers ---
void ensureWifiIfConnected() {
  if (!CONNECTED) return;
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void mqttSetupIfNeeded() {
  if (mqttClientId.length() == 0) {
    uint64_t mac = ESP.getEfuseMac();
    uint32_t chipId = (uint32_t)(mac & 0xFFFFFF);
    char buf[32];
    snprintf(buf, sizeof(buf), "cardioia-esp32-%06X", chipId);
    mqttClientId = String(buf);
  }
  // Define sempre o servidor
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
}

void mqttEnsureConnected() {
  if (!CONNECTED) return;
  if (WiFi.status() != WL_CONNECTED) return;
  mqttSetupIfNeeded();
  unsigned long now = millis();
  if (mqtt.connected()) return;
  if (now < mqttNextRetry) return;

  if (mqtt.connect(mqttClientId.c_str(), MQTT_USER, MQTT_PASS)) {
    Serial.println(F("MQTT_CONNECTED"));
    mqttBackoffMs = 1000; // reset backoff
  } else {
    Serial.println(F("MQTT_CONNECT_FAIL"));
    // backoff exponencial com clamp a 30s
    unsigned long next = mqttBackoffMs * 2;
    if (next > 30000UL) next = 30000UL;
    mqttBackoffMs = next;
    mqttNextRetry = now + mqttBackoffMs;
  }
}

void mqttLoopIfConnected() {
  if (mqtt.connected()) {
    mqtt.loop();
  }
}

void mqttPublishLineIfPossible(const String& line) {
  if (!mqtt.connected()) return;
  bool ok = mqtt.publish(MQTT_TOPIC, line.c_str());
  if (ok) Serial.println(F("MQTT_PUBLISH_OK"));
  else Serial.println(F("MQTT_PUBLISH_FAIL"));
}

// --- Processa comandos seriais ---
void handleSerialCommands() {
  while (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.equalsIgnoreCase("ONLINE")) {
      CONNECTED = true;
      Serial.println(F("[STATE] CONNECTED=true (ONLINE)"));
      // Tenta conectar WiFi/MQTT (TLS)
      ensureWifiIfConnected();
      mqttEnsureConnected();
      // Tenta flush do buffer em RAM, se já conectado
      if (mqtt.connected()) {
        ramFlushPublish();
      }
    } else if (cmd.equalsIgnoreCase("OFFLINE")) {
      CONNECTED = false;
      Serial.println(F("[STATE] CONNECTED=false (OFFLINE)"));
    } else if (cmd.length() > 0) {
      Serial.print(F("[WARN] Unknown command: ")); Serial.println(cmd);
    }
  }
}
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("Booting..."));
  Serial.println(F("Digite ONLINE no Serial para conectar WiFi+MQTT (TLS)."));
  Serial.println(F("Clique rápido no botão (GPIO4) para aumentar BPM; ajuste o DHT22 > 38 °C para alerta."));

  // Pinos
  pinMode(PIN_BTN, INPUT); // botão com pull-down externo
  lastBtnState = digitalRead(PIN_BTN);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN), onButtonChange, CHANGE);

  // DHT
  dht.setup(PIN_DHT, DHTesp::DHT22);

  // Tempos
  windowStart = millis();

  // TLS sem verificação de certificado (demo). Em produção, configure a CA.
  tlsClient.setInsecure();
}

void loop() {
  handleSerialCommands();

  // Conectividade (modo conectado)
  if (CONNECTED) {
    ensureWifiIfConnected();
    mqttEnsureConnected();
  }
  mqttLoopIfConnected();

  // Leituras periódicas
  readDhtIfDue();

  // Verifica janela de BPM
  bool windowDone = computeBpmIfWindowDone();
  if (windowDone) {
    uint32_t ts = millis();
    String json = makeSampleJson(ts, lastTemp, lastHum, lastBpm, CONNECTED);

    if (CONNECTED) {
      // Publica diretamente na nuvem (MQTT) e loga no Serial
      Serial.print(F("BPM janela= ")); Serial.println(lastBpm);
      Serial.println(json);
      // Primeiro tenta limpar backlog em RAM
      if (mqtt.connected()) {
        ramFlushPublish();
      }
      mqttPublishLineIfPossible(json);
    } else {
      // Offline: enfileira em RAM
      ramEnqueue(json);
      Serial.print(F("BPM janela= ")); Serial.println(lastBpm);
      Serial.print(F("[OFFLINE] queued RAM size=")); Serial.println((unsigned long)ramCount);
    }
  }
}
