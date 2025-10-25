# Relatório – Parte 1 (Edge ESP32)

Este relatório descreve a arquitetura e o funcionamento do módulo de borda (ESP32) do projeto CardioIA, cobrindo o fluxo de coleta, o buffer offline em RAM e o mecanismo de flush para a nuvem via MQTT; a estratégia de resiliência com limitação do buffer; e como simular estados ONLINE/OFFLINE via monitor serial. Ao final, descrevemos as capturas de tela esperadas.

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
  - Se `CONNECTED = true`: a amostra corrente é publicada via MQTT (TLS) e registrada no Serial. Antes, o dispositivo tenta dar flush do backlog em RAM se o MQTT já estiver conectado.
  - Se `CONNECTED = false`: a amostra é adicionada a um buffer em RAM (ring buffer) que mantém as amostras mais recentes.
- **Logs no Serial (atuais)**
  - `RAM_FLUSH <n>`: foram enviados `<n>` registros do backlog do buffer em RAM.
  - `MQTT_CONNECTED` / `MQTT_CONNECT_FAIL`: status da conexão MQTT.
  - `MQTT_PUBLISH_OK` / `MQTT_PUBLISH_FAIL`: resultado da publicação.
  - `[OFFLINE] queued RAM size=<n>`: tamanho atual do buffer em RAM quando offline.

Arquivos relevantes:
- Código principal: `apps/edge-esp32/src/main.cpp`
- Diagrama Wokwi: `apps/edge-esp32/wokwi/diagram.json`

## 2. Estratégia de resiliência e restrição de armazenamento

- **Resiliência a desconexões (RAM)**
  - O sistema trabalha em modo store-and-forward com buffer em RAM: quando offline, cada amostra é mantida em uma fila circular na memória. Quando retoma `ONLINE` e o MQTT conecta, o backlog é enviado (`RAM_FLUSH <n>`), seguido do envio da amostra atual.
- **Limite do buffer (200 amostras)**
  - Para limitar o uso de memória, o buffer em RAM mantém no máximo 200 amostras mais recentes. Ao exceder, as mais antigas são descartadas (ring buffer).
  - Justificativa:
    - Simplicidade e velocidade (sem uso de flash) para a demonstração.
    - 200 janelas de 10s cobrem ~33 minutos de coleta contínua.
    - Evita desgaste e gerenciamento de SPIFFS nesta fase da solução.

## 3. Simulação de ONLINE/OFFLINE via monitor serial

- Comandos suportados (terminados por Enter):
  - `ONLINE` → `CONNECTED = true` e tentativa imediata de conectar WiFi/MQTT (TLS). Se conectado, executa flush do buffer em RAM.
  - `OFFLINE` → `CONNECTED = false` (amostras seguintes vão para o buffer em RAM).
- Passo a passo sugerido:
  1. Inicie o monitor serial a 115200 baud.
  2. Digite `OFFLINE` e pressione Enter.
  3. Aguarde 10s (fechamento de 1 janela) e observe `[OFFLINE] queued RAM size=<n>` crescendo.
  4. Pressione o botão algumas vezes durante a janela para simular batimentos.
  5. Digite `ONLINE` e pressione Enter: observe a emissão da amostra atual e `RAM_FLUSH <n>` (backlog).

## 4. Capturas de tela esperadas (descrever)

  - Mostrando `esp32-devkit-v1`, `dht22` ligado ao `GPIO 15` (VCC=3.3V, GND=GND, DATA=15), e botão no `GPIO 4` com resistor de 10k para GND e ligação ao 3.3V.
- **Monitor serial – OFFLINE**
  - Linhas com `[OFFLINE] queued RAM size=<n>` crescendo.
- **Monitor serial – ONLINE**
  - Linha JSON da amostra atual e, em seguida, `RAM_FLUSH <n>` com vários registros do backlog.

---

Referências de arquivos:
- `apps/edge-esp32/src/main.cpp`
- `apps/edge-esp32/wokwi/diagram.json`
- `apps/edge-esp32/platformio.ini`

---

## 5. Evidências (prints)

- **Link do projeto no Wokwi**: https://wokwi.com/projects/445438493925842945
- **Capturas a inserir:**
  - Monitor Serial – OFFLINE (mostrar `[OFFLINE] queued RAM size=<n>` crescendo).
  - Monitor Serial – ONLINE (mostrar uma linha JSON e `RAM_FLUSH <n>`).
  - Diagrama Wokwi com ligações (DHT22 em GPIO 15; botão em GPIO 4 com pulldown 10k).

### Placeholders de imagem

- **Serial OFFLINE**

  ![Serial OFFLINE](../assets/parte1/serial_offline.png)

- **Serial ONLINE (flush)**

  ![Serial ONLINE](../assets/parte1/serial_online_flush.png)

- **Diagrama Wokwi**

  ![Diagrama Wokwi](../assets/parte1/wokwi_diagrama.png)
