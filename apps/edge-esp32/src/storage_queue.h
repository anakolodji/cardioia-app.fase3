#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>

// Nome do arquivo de fila no SPIFFS
static const char* QUEUE_FILE = "/queue.ndjson";

// Garante que o arquivo de fila existe
inline bool ensureQueueFile() {
  if (!SPIFFS.exists(QUEUE_FILE)) {
    File f = SPIFFS.open(QUEUE_FILE, FILE_WRITE);
    if (!f) {
      return false;
    }
    f.close();
  }
  return true;
}

// Inicializa o SPIFFS e garante a fila
inline bool initStorage() {
  if (!SPIFFS.begin(true)) { // true: format-on-fail
    return false;
  }
  return ensureQueueFile();
}

// Conta linhas do arquivo de forma eficiente
inline size_t getQueueSizeLines() {
  File f = SPIFFS.open(QUEUE_FILE, FILE_READ);
  if (!f) return 0;
  size_t lines = 0;
  while (f.available()) {
    if (f.read() == '\n') lines++;
  }
  f.close();
  return lines;
}

// Remove as primeiras 'drop' linhas do arquivo (ring buffer simples)
inline bool dropOldestLines(size_t drop) {
  if (drop == 0) return true;
  File in = SPIFFS.open(QUEUE_FILE, FILE_READ);
  if (!in) return false;

  // Pula 'drop' quebras de linha
  while (drop > 0 && in.available()) {
    if (in.read() == '\n') drop--;
  }

  // Copia restante para arquivo temporário
  const char* TMP_FILE = "/queue.tmp";
  File out = SPIFFS.open(TMP_FILE, FILE_WRITE);
  if (!out) { in.close(); return false; }

  uint8_t buf[512];
  while (in.available()) {
    size_t n = in.read(buf, sizeof(buf));
    if (n > 0) out.write(buf, n);
  }
  in.close();
  out.close();

  // Substitui original
  SPIFFS.remove(QUEUE_FILE);
  if (!SPIFFS.rename(TMP_FILE, QUEUE_FILE)) {
    // Tenta recriar vazio se falhar
    ensureQueueFile();
    return false;
  }
  return true;
}

// Enfileira uma linha (com \n no final). Aplica cap de maxLines (ring buffer).
inline bool enqueueLine(const String& line, size_t maxLines, size_t& outSizeAfter) {
  if (!ensureQueueFile()) return false;

  // Anexa
  File f = SPIFFS.open(QUEUE_FILE, FILE_APPEND);
  if (!f) return false;
  bool ok = (f.print(line) > 0);
  if (line.length() == 0 || line[line.length()-1] != '\n') {
    ok &= (f.print("\n") > 0);
  }
  f.close();
  if (!ok) return false;

  // Verifica tamanho e aplica cap
  size_t sz = getQueueSizeLines();
  if (sz > maxLines) {
    size_t drop = sz - maxLines;
    dropOldestLines(drop);
    sz = getQueueSizeLines();
  }
  outSizeAfter = sz;
  return true;
}

// Envia todas as linhas para 'out' (normalmente Serial) e limpa a fila.
// Retorna quantidade de linhas enviadas.
inline size_t flushQueue(Stream& out) {
  if (!SPIFFS.exists(QUEUE_FILE)) return 0;
  File f = SPIFFS.open(QUEUE_FILE, FILE_READ);
  if (!f) return 0;

  size_t count = 0;
  String line;
  while (f.available()) {
    line = f.readStringUntil('\n');
    if (line.length() == 0) {
      // pular linhas vazias
      continue;
    }
    out.println(line);
    count++;
  }
  f.close();

  // Limpa a fila após envio
  SPIFFS.remove(QUEUE_FILE);
  ensureQueueFile();
  return count;
}
