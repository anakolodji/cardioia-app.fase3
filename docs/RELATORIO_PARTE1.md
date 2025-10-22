# Relatório – Parte 1 (Edge ESP32)

Este relatório descreve a arquitetura e o funcionamento do módulo de borda (ESP32) do projeto CardioIA, cobrindo o fluxo de coleta, a fila persistente e o mecanismo de flush; a estratégia de resiliência com limitação de armazenamento; e como simular estados ONLINE/OFFLINE via monitor serial. Ao final, descrevemos as capturas de tela esperadas.

## 1. Fluxo de coleta → fila → flush

- **Coleta de sinais**
  - Temperatura/Umidade: sensor DHT22 no `GPIO 15`, lido a cada 2 segundos (`DHTesp`).
  - Batimentos: botão no `GPIO 4` com pull-down (10k para GND). Cada borda de subida conta 1 pulso. Em janelas de 10 segundos, calcula-se `BPM = pulsos * 6`.
- **Amostra unificada (JSON)**
  - Estrutura de cada amostra (linha única):
    ```json
    {"ts":<millis>,"temp":<C>,"hum":<%>,"bpm":<int>,"connected":<bool>}
    ```
  - `ts` é o timestamp local em milissegundos desde o boot (`millis()`).
- **Decisão de destino**
  - Se `CONNECTED = true`: a amostra corrente é enviada via `Serial.println()` e, em seguida, é executado `flushQueue()` para enviar todo o backlog persistido, limpando o arquivo após envio.
  - Se `CONNECTED = false`: a amostra é adicionada a um arquivo NDJSON (`/queue.ndjson`) em SPIFFS por meio de `enqueueLine()`.
- **Logs no Serial**
  - `ENQUEUE`: amostra armazenada localmente com sucesso.
  - `FLUSH <n>`: foram enviados `<n>` registros do backlog (após ficar online ou manualmente durante loop online).
  - `QUEUE_SIZE <n>`: tamanho atual (linhas) da fila persistente.

Arquivos relevantes:
- Código principal: `apps/edge-esp32/src/main.cpp`
- Utilitários de fila: `apps/edge-esp32/src/storage_queue.h`
- Diagrama Wokwi: `apps/edge-esp32/wokwi/diagram.json`

## 2. Estratégia de resiliência e restrição de armazenamento

- **Resiliência a desconexões**
  - O sistema trabalha em modo store-and-forward: quando offline, cada amostra é persistida em SPIFFS (formato NDJSON). Quando retoma `ONLINE`, o backlog é enviado e o arquivo é limpo (flush bem-sucedido).
- **Limite de armazenamento (10.000 amostras)**
  - Para evitar exaustão de espaço em SPIFFS, foi definido um limite de 10.000 linhas (amostras). Ao exceder, as linhas mais antigas são descartadas (ring buffer por linhas), preservando as amostras mais recentes.
  - Justificativa:
    - Em janelas de 10s, temos até ~6 amostras/minuto (na prática, 1 amostra por janela de 10s). Com limite de 10.000, o buffer cobre muitas horas/dias de coleta, a depender da cadência e disponibilidade de rede.
    - Mantém o consumo de flash previsível e evita fragmentação excessiva.
    - Simplicidade operacional: o mecanismo por linhas é robusto e de fácil limpeza após flush.

## 3. Simulação de ONLINE/OFFLINE via monitor serial

- Comandos suportados (terminados por Enter):
  - `ONLINE` → `CONNECTED = true` e execução imediata de `flushQueue()`.
  - `OFFLINE` → `CONNECTED = false` (amostras seguintes vão para a fila em SPIFFS).
- Passo a passo sugerido:
  1. Inicie o monitor serial a 115200 baud.
  2. Digite `OFFLINE` e pressione Enter.
  3. Aguarde 10s (fechamento de 1 janela) e observe `ENQUEUE` e `QUEUE_SIZE` crescendo.
  4. Pressione o botão algumas vezes durante a janela para simular batimentos.
  5. Digite `ONLINE` e pressione Enter: observe a emissão da amostra atual e `FLUSH <n>` (backlog), seguido de `QUEUE_SIZE` reduzindo.

## 4. Capturas de tela esperadas (descrever)

- **Diagrama no Wokwi**
  - Mostrando `esp32-devkit-v1`, `dht22` ligado ao `GPIO 15` (VCC=3.3V, GND=GND, DATA=15), e botão no `GPIO 4` com resistor de 10k para GND e ligação ao 3.3V.
- **Monitor serial – OFFLINE**
  - Linhas com `ENQUEUE` e crescimento de `QUEUE_SIZE`. Opcionalmente, a amostra JSON não é emitida no Serial quando offline (vai para o arquivo), apenas logs de enfileiramento.
- **Monitor serial – ONLINE**
  - Linha JSON da amostra atual e, em seguida, `FLUSH <n>` com vários registros do backlog; `QUEUE_SIZE` tendendo a zero.
- **Arquivo de fila (SPIFFS)**
  - Visualização (via ferramenta SPIFFS ou logs) mostrando que o arquivo `/queue.ndjson` existe e é recriado após flush.

---

Referências de arquivos:
- `apps/edge-esp32/src/main.cpp`
- `apps/edge-esp32/src/storage_queue.h`
- `apps/edge-esp32/wokwi/diagram.json`
- `apps/edge-esp32/platformio.ini`
