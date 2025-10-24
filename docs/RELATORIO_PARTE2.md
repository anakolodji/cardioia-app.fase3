# Relatório – Parte 2 (MQTT + Node-RED + Dashboard/Alertas)

Este relatório detalha a arquitetura de telemetria em tempo (quase) real da solução CardioIA para a Parte 2: publicação via MQTT a partir do ESP32 (Wokwi), consumo no Node-RED, visualização em dashboard e alertas, além de um guia para envio dos dados ao Grafana Cloud utilizando InfluxDB como destino.

## 1. Diagrama do fluxo MQTT

```mermaid
flowchart LR
  subgraph Edge[ESP32]
    A[Coleta: DHT22 + Pulsos (BPM)] --> B[JSON/NDJSON]
    B --> C[MQTT Publish (QoS0)]
    B -. OFFLINE .-> D[SPIFFS queue.ndjson]
    D -. ONLINE/Flush .-> C
  end

  C -->|broker.hivemq.com:1883| E[(MQTT Broker)]
  E --> F[Node-RED]
  F --> G[Dashboard (ui_chart/ui_gauge/ui_text+LED)]
  F --> H[Alertas (toast/status)]
  F --> I[Saída para InfluxDB / Grafana Cloud]
```

- **ESP32** (`apps/edge-esp32/src/main.cpp`):
  - Publica JSON no tópico `cardioia/ana/v1/vitals` quando `CONNECTED=true` (WiFi+MQTT).
  - Resiliência: enfileira todas as amostras em `SPIFFS:/queue.ndjson` (Opção A) e faz flush quando online (Serial + MQTT).
- **Broker**: `broker.hivemq.com:1883` (público, sem TLS).
- **Node-RED** (`apps/dashboard-nodered/flows.json`):
  - Consome do tópico, normaliza campos, alimenta Dashboard e dispara alertas.
  - Pode encaminhar os dados para InfluxDB/Grafana.

## 2. Payload e política de QoS

- **Tópico**: `cardioia/ana/v1/vitals`
- **QoS**: 0 (entrega “melhor esforço”, sem confirmação). Adequado para telemetria de alta frequência com tolerância a perdas esporádicas.
- **Retained**: não utilizado (dados de streaming).
- **Formato JSON (linha única)**:
  ```json
  {"ts": <millis>, "temp": <C>, "hum": <percent>, "bpm": <int>, "connected": <bool>}
  ```
  - `ts`: timestamp em milissegundos (millis do ESP32).
  - `temp`: temperatura em °C (float com 2 casas).
  - `hum`: umidade relativa em % (float com 2 casas).
  - `bpm`: batimentos por minuto (inteiro, janela de 10s * 6).
  - `connected`: estado da conectividade lógica (Serial/WiFi/MQTT).

### Reconexão MQTT e backoff
- Implementado no `main.cpp` com `PubSubClient`:
  - Tentativas com **backoff exponencial** (1s, 2s, 4s, … até 30s máx.).
  - Logs: `MQTT_CONNECTED`, `MQTT_CONNECT_FAIL`, `MQTT_PUBLISH_OK/FAIL`.

## 3. Limiares e alertas no Node-RED

- **Normalização** (`function normalize vitals` em `flows.json`):
  - Entrada: objeto `{ts, temp, hum, bpm}` vindo do `json` node.
  - Saídas:
    - Saída 1: `payload = bpm` (gráfico)
    - Saída 2: `payload = temp` (medidor)
    - Saída 3: `payload = status`, `msg.color` para LED e toast
    - Saída 4: mensagem completa para debug
- **Regras de status**:
  - `OK`
  - `ALTA_TEMP` se `temp > 38`
  - `TAQUICARDIA` se `bpm > 120`
  - `ALTA_TEMP+TAQUICARDIA` se ambos
- **Dashboard** (node-red-dashboard v1):
  - `ui_chart` (linha) de BPM, janela de 10 minutos.
  - `ui_gauge` de temperatura (20–45 °C, zonas de cor configuradas).
  - `ui_text` e `ui_template` (LED colorido) para status.
- **Alertas visuais**:
  - `switch (status != OK)` → `ui_toast` exibe notificação 4s no canto superior direito.
  - O LED muda de cor conforme `msg.color`.

## 4. Envio ao Grafana Cloud via Node-RED + InfluxDB

A seguir, um guia para encaminhar os dados recebidos pelo Node-RED ao Grafana Cloud utilizando InfluxDB como base de séries temporais.

### 4.1. Opção A – Usar InfluxDB Cloud (recomendado)
- Crie uma conta no **InfluxDB Cloud** (free tier) e obtenha:
  - `Org` (organização)
  - `Bucket` (ex.: `cardioia`)
  - `Token` (com permissão de escrita)
  - `URL` do Influx (ex.: `https://us-east-1-1.aws.cloud2.influxdata.com`)
- No Grafana Cloud, adicione uma **data source** InfluxDB com esses dados (Flux API v2).
- No Node-RED, instale os nós InfluxDB:
  - Menu → Manage palette → Install → procure por `node-red-contrib-influxdb`.
- Configure um nó `influxdb out` (v2):
  - URL, Org, Bucket e Token conforme sua conta.
- Antes do `influxdb out`, crie um `function` para transformar a mensagem:
  ```javascript
  // Input: msg.ts, msg.temp, msg.hum, msg.bpm
  // Output para Influx v2 (node-red-contrib-influxdb)
  msg.measurement = "vitals";
  msg.tags = { device: "cardioia-esp32" };
  msg.fields = {
    temp: Number(msg.temp),
    hum: Number(msg.hum),
    bpm: Number(msg.bpm)
  };
  msg.timestamp = Number(msg.ts) * 1e6; // nanos: millis * 1e6
  return msg;
  ```
- Ligue a saída 4 (`normalized` debug) do `function normalize` a esse `function` e depois ao `influxdb out`.
- No Grafana Cloud, crie um dashboard com painéis para `temp`, `hum` e `bpm` consultando o bucket.

### 4.2. Opção B – InfluxDB local (Docker) + Grafana Cloud (gateway)
- Suba um InfluxDB local para testes:
  ```bash
  docker run -d --name influxdb -p 8086:8086 \
    -e DOCKER_INFLUXDB_INIT_MODE=setup \
    -e DOCKER_INFLUXDB_INIT_USERNAME=admin \
    -e DOCKER_INFLUXDB_INIT_PASSWORD=admin123 \
    -e DOCKER_INFLUXDB_INIT_ORG=cardioia \
    -e DOCKER_INFLUXDB_INIT_BUCKET=vitals \
    -e DOCKER_INFLUXDB_INIT_ADMIN_TOKEN=local-token \
    influxdb:2
  ```
- No Node-RED, configure o `influxdb out` apontando para `http://localhost:8086`, Org `cardioia`, Bucket `vitals`, Token `local-token`.
- No Grafana Cloud, adicione um **Cloud Access** (ou use um Grafana local para validar) apontando para seu InfluxDB local através de um Agent/Proxy se necessário.

### 4.3. Esquema de medições sugerido
- Measurement: `vitals`
- Tags: `device`, `status`
- Fields (float/int): `temp`, `hum`, `bpm`
- Timestamp: `ts` (nanos)

### 4.4. Boas práticas
- Evite publicar todos os campos como string; utilize numéricos para métricas.
- Garanta idempotência de escrita (o `ts` único ajuda a evitar duplicidade).
- Tenha um nó de buffer/retry no Node-RED se o Influx estiver offline (ex.: `delay`/`file`), se necessário.

## 5. Considerações de segurança e limites
- O broker público (`broker.hivemq.com:1883`) é apenas para demonstração.
  - Em produção: utilize broker próprio com TLS, autenticação (username/password, certificados), ACL de tópicos e retenção adequada.
- QoS 0 é suficiente para demo; para produção, avalie QoS 1/2 e custos de latência/armazenamento.
- Dados sensíveis de saúde devem ser tratados conforme LGPD/HIPAA (~PII, criptografia, retenção, consentimento).

## 6. Referências
- Código ESP32: `apps/edge-esp32/src/main.cpp`, `apps/edge-esp32/src/storage_queue.h`, `apps/edge-esp32/platformio.ini`
- Flow Node-RED: `apps/dashboard-nodered/flows.json`
- Documentação:
  - PubSubClient: https://pubsubclient.knolleary.net/
  - Node-RED Dashboard: https://flows.nodered.org/node/node-red-dashboard
  - HiveMQ Public Broker: https://www.hivemq.com/public-mqtt-broker/
  - InfluxDB v2: https://docs.influxdata.com/
  - Grafana Cloud: https://grafana.com/products/cloud/

---

## 7. Evidências (prints)

- **Link do projeto no Wokwi**: https://wokwi.com/projects/445438493925842945
- **Capturas a inserir:**
  - Dashboard Node-RED com gráfico BPM, gauge Temp, LED de status e toast de alerta.
  - HiveMQ WebSocket Client com mensagens no tópico `cardioia/ana/v1/vitals`.
  - Opcional: painéis do Grafana Cloud lendo do InfluxDB (`vitals.temp`, `vitals.bpm`).

### Placeholders de imagem

- **Dashboard Node-RED**

  ![Dashboard Node-RED](../assets/parte2/nodered_dashboard.png)

- **HiveMQ WebSocket Client**

  ![HiveMQ WebSocket](../assets/parte2/hivemq_ws.png)

- **Grafana Cloud (opcional)**

  ![Grafana Painéis](../assets/parte2/grafana_dashboards.png)
