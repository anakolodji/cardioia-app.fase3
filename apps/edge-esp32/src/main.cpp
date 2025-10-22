#include <Arduino.h>
#include <DHTesp.h>
#include <FS.h>
#include <SPIFFS.h>

#include "storage_queue.h"

// --- Configurações de pinos ---
static const int PIN_DHT = 15;     // DHT22 no GPIO 15
static const int PIN_BTN = 4;      // Botão no GPIO 4 (pull-down externo)

// --- Janelas e tempos ---
static const uint32_t DHT_INTERVAL_MS = 2000;   // leitura a cada 2s
static const uint32_t BPM_WINDOW_MS  = 10000;  // janela de 10s

// --- Capacidade da fila ---
static const size_t QUEUE_MAX_LINES = 10000;

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

// --- Processa comandos seriais ---
void handleSerialCommands() {
  while (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.equalsIgnoreCase("ONLINE")) {
      CONNECTED = true;
      Serial.println(F("[STATE] CONNECTED=true (ONLINE)"));
      // Força flush quando ficar online
      size_t sent = flushQueue(Serial);
      if (sent > 0) {
        Serial.print(F("FLUSH ")); Serial.println((unsigned long)sent);
      }
      size_t qsz = getQueueSizeLines();
      Serial.print(F("QUEUE_SIZE ")); Serial.println((unsigned long)qsz);
    } else if (cmd.equalsIgnoreCase("OFFLINE")) {
      CONNECTED = false;
      Serial.println(F("[STATE] CONNECTED=false (OFFLINE)"));
      size_t qsz = getQueueSizeLines();
      Serial.print(F("QUEUE_SIZE ")); Serial.println((unsigned long)qsz);
    } else if (cmd.length() > 0) {
      Serial.print(F("[WARN] Unknown command: ")); Serial.println(cmd);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("Booting..."));

  // Inicializa SPIFFS e cria fila
  if (!initStorage()) {
    Serial.println(F("[ERROR] SPIFFS init failed"));
  } else {
    Serial.println(F("[OK] SPIFFS ready"));
  }

  // Pinos
  pinMode(PIN_BTN, INPUT); // botão com pull-down externo
  lastBtnState = digitalRead(PIN_BTN);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN), onButtonChange, CHANGE);

  // DHT
  dht.setup(PIN_DHT, DHTesp::DHT22);

  // Tempos
  lastDhtRead = 0;
  windowStart = millis();

  // Estado inicial da fila
  ensureQueueFile();
  Serial.print(F("QUEUE_SIZE ")); Serial.println((unsigned long)getQueueSizeLines());
}

void loop() {
  handleSerialCommands();

  // Leituras periódicas
  readDhtIfDue();

  // Verifica janela de BPM
  bool windowDone = computeBpmIfWindowDone();
  if (windowDone) {
    uint32_t ts = millis();
    String json = makeSampleJson(ts, lastTemp, lastHum, lastBpm, CONNECTED);

    if (CONNECTED) {
      // Opção A: sempre enfileira a amostra e depois faz flush total
      size_t afterSz = 0;
      if (enqueueLine(json, QUEUE_MAX_LINES, afterSz)) {
        Serial.println(F("ENQUEUE"));
      } else {
        Serial.println(F("[ERROR] ENQUEUE failed"));
      }

      size_t sent = flushQueue(Serial);
      Serial.print(F("FLUSH ")); Serial.println((unsigned long)sent);
      size_t qsz = getQueueSizeLines();
      Serial.print(F("QUEUE_SIZE ")); Serial.println((unsigned long)qsz);
    } else {
      // Armazena localmente
      size_t afterSz = 0;
      if (enqueueLine(json, QUEUE_MAX_LINES, afterSz)) {
        Serial.println(F("ENQUEUE"));
        Serial.print(F("QUEUE_SIZE ")); Serial.println((unsigned long)afterSz);
      } else {
        Serial.println(F("[ERROR] ENQUEUE failed"));
      }
    }
  }
}
