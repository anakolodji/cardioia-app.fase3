# Edge ESP32 (CardioIA)

Este diretório contém o firmware (Arduino Framework) para o ESP32 DevKit V1, além do diagrama do Wokwi para simulação. O código coleta temperatura/umidade (DHT22) e batimentos (botão) e realiza buffering resiliente em SPIFFS quando offline.

- Código-fonte: `src/main.cpp`, `src/storage_queue.h`
- Simulação: `wokwi/diagram.json`
- Build local: `platformio.ini`

## Funcionalidades
- Leitura DHT22 a cada 2s (GPIO 15).
- Detecção de batimentos por botão (GPIO 4), janela de 10s → `BPM = pulsos * 6`.
- Amostra JSON linha única: `{"ts":<millis>,"temp":<C>,"hum":<%>,"bpm":<int>,"connected":<bool>}`.
- Resiliência: quando offline, as amostras vão para `SPIFFS:/queue.ndjson` (NDJSON com ring buffer de 10.000 linhas). Quando online, envia backlog e limpa.
- Comandos seriais: `ONLINE` / `OFFLINE`.
- Logs: `ENQUEUE`, `FLUSH <n>`, `QUEUE_SIZE <n>`.

## Rodando no Wokwi
1. Abra `apps/edge-esp32/wokwi/diagram.json` no Wokwi (Web ou extensão VSCode).
2. Componentes:
   - `esp32-devkit-v1`.
   - `dht22` com `DATA -> GPIO 15`, `VCC -> 3.3V`, `GND -> GND`.
   - Botão no `GPIO 4` com pull-down: um terminal ao `GPIO 4`, outro ao `3.3V`, resistor `10k` de `GPIO 4` a `GND`.
3. Inicie a simulação.
4. Abra o monitor serial (115200 baud).
5. Use os comandos:
   - Digite `OFFLINE` e pressione Enter.
     - Aguarde 10s (uma janela) para ver `ENQUEUE` e `QUEUE_SIZE` crescer.
   - Digite `ONLINE` e pressione Enter.
     - Deve aparecer a amostra atual e `FLUSH <n>` com esvaziamento do backlog; `QUEUE_SIZE` deve reduzir.
6. Para simular batimentos, pressione o botão. Cada pressão (borda de subida) contará como 1 pulso.

Observações (Wokwi):
- `dependencies: ["dht"]` já está no `diagram.json`.
- O botão gera `HIGH` no GPIO 4 quando pressionado (por ligação ao 3.3V e pull-down de 10k a GND).

## Rodando localmente (PlatformIO)
Pré-requisitos:
- VSCode + extensão PlatformIO
- Placa: ESP32 DevKit V1 (ou compatível)

Passos:
1. Abra a pasta `apps/edge-esp32/` no VSCode (como projeto PlatformIO).
2. Conecte a placa via USB.
3. Verifique `platformio.ini`:
   - `board = esp32dev`
   - `framework = arduino`
   - `lib_deps = beegee_tokyo/DHT sensor library for ESPx`
4. Compile (PlatformIO: Build) e faça upload (Upload).
5. Abra o monitor serial (115200) e interaja com os comandos `ONLINE` / `OFFLINE`.

Fiação no hardware real (se necessário):
- DHT22: `VCC -> 3.3V`, `GND -> GND`, `DATA -> GPIO 15` (use resistor de pull-up de 10k para 3.3V, se seu módulo não tiver interno).
- Botão: um terminal ao `GPIO 4`, outro ao `3.3V`, resistor `10k` de `GPIO 4` para `GND` (pull-down).

## Formato de saída e logs
Exemplo de amostra:
```
{"ts":123456,"temp":26.50,"hum":52.10,"bpm":72,"connected":true}
```
Logs auxiliares:
```
ENQUEUE
FLUSH 42
QUEUE_SIZE 58
[STATE] CONNECTED=true (ONLINE)
```

## Estrutura de arquivos
```
apps/edge-esp32/
├─ src/
│  ├─ main.cpp
│  └─ storage_queue.h
├─ wokwi/
│  └─ diagram.json
└─ platformio.ini
```

## Solução de problemas
- `SPIFFS init failed`: verifique partições e suporte SPIFFS da placa. No primeiro boot, a formatação automática é tentada.
- Sem leituras do DHT: confira o pino (GPIO 15) e alimentação (3.3V). Em hardware, pode ser necessário pull-up no DATA.
- BPM sempre 0: verifique o botão e o fio no `GPIO 4`. O contador incrementa em cada borda de subida.
