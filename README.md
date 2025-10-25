# Edge ESP32 (CardioIA)

Este diretório contém o firmware (Arduino Framework) para o ESP32 DevKit V1, além do diagrama do Wokwi. O código coleta temperatura/umidade (DHT22) e batimentos (botão) e faz buffer em RAM quando offline, publicando o backlog quando a rede/MQTT conectam.

- Código-fonte: `apps/edge-esp32/src/main.cpp`
- Simulação: `apps/edge-esp32/wokwi/diagram.json`
- Build local: `apps/edge-esp32/platformio.ini`

## Funcionalidades
- Leitura DHT22 a cada 2s (GPIO 15).
- Detecção de batimentos por botão (GPIO 4), janela de 10s → `BPM = pulsos * 6`.
- Amostra JSON linha única: `{"ts":<millis>,"temp":<C>,"hum":<%>,"bpm":<int>,"connected":<bool>}`.
- Resiliência: quando offline, amostras vão para fila em RAM (ring buffer). Quando online, envia backlog e a amostra atual.
- Comandos seriais: `ONLINE` / `OFFLINE`.
- Logs: `RAM_FLUSH <n>`, `MQTT_CONNECTED`, `MQTT_PUBLISH_OK`.

## Lógica da aplicação
- Leitura periódica do DHT22 e contagem de pulsos no botão.
- A cada janela de 10s, calcula `BPM = pulsos * 6` e monta JSON da amostra.
- Estado `CONNECTED` controlado via Serial (`ONLINE`/`OFFLINE`).
- Se offline: enfileira amostra em buffer RAM (ring buffer, até 200 amostras).
- Se online: tenta conectar WiFi e MQTT (HiveMQ Cloud TLS 8883), faz flush do backlog (`RAM_FLUSH <n>`) e publica amostra atual (`MQTT_PUBLISH_OK`).
- Reconexão MQTT com backoff exponencial (1s→30s) e logs `MQTT_CONNECT_FAIL`/`MQTT_CONNECTED`.

## Segredos (config.h)
- Crie `apps/edge-esp32/src/config.h` a partir de `config.h.example`. Não versionar.
- Define: `WIFI_SSID`, `WIFI_PASS`, `MQTT_HOST`, `MQTT_PORT` (8883 para HiveMQ Cloud/TLS), `MQTT_USER`, `MQTT_PASS`.

## Rodando no Wokwi (apenas Serial)
Projeto no Wokwi: https://wokwi.com/projects/445438493925842945

1. Abra `apps/edge-esp32/wokwi/diagram.json`.
2. Componentes: `esp32-devkit-v1`, `dht22` (DATA→GPIO 15), botão (GPIO 4 com pulldown 10k).
3. Start → abra Serial Monitor 115200.
4. Digite `ONLINE` para conectar (ou `OFFLINE` para simular fila em RAM).
5. A cada ~10s imprime BPM, leitura DHT e a linha JSON. Se online, publica (ver `MQTT_PUBLISH_OK`).

Observações (Wokwi):
- `wokwi/libraries.txt`: `PubSubClient`, `DHT sensor library for ESPx`.
- Se usar o Wokwi Web e não quiser subir o `config.h` com segredos, teste somente Serial (sem MQTT) ou use usuário temporário.

## Rodando localmente (PlatformIO)
Pré-requisitos:
- VSCode + PlatformIO
- Placa: ESP32 DevKit V1

Passos:
1. Abrir `apps/edge-esp32/` no VSCode.
2. Build/Upload (PlatformIO).
3. Monitor Serial 115200 → comandos `ONLINE` / `OFFLINE`.

## Formato de saída e logs
Exemplo de amostra:
```
{"ts":123456,"temp":26.50,"hum":52.10,"bpm":72,"connected":true}
```
Logs auxiliares:
```
RAM_FLUSH 42
MQTT_CONNECTED
MQTT_PUBLISH_OK
```

## Estrutura
```
apps/edge-esp32/
├─ src/
│  ├─ main.cpp
│  ├─ config.h.example
│  └─ config.h            # não versionar
├─ wokwi/
│  ├─ diagram.json
│  └─ libraries.txt
└─ platformio.ini
```

## Solução de problemas
- Bibliotecas Wokwi: use `PubSubClient` e `DHT sensor library for ESPx` em `libraries.txt`.
- Sem leituras do DHT: revisar pino (15) e alimentação. Em hardware, pode precisar pull-up no DATA.
- BPM sempre 0: checar ligação do botão (GPIO 4) e pulldown 10k.

- **Capturas na pasta do Google Drive atraves do link:**: https://drive.google.com/file/d/1weEoeO-TcD4P9PwUW7FBi2mkm1K1MwJb/view?usp=drive_link