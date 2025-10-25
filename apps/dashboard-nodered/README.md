# Node-RED Dashboard – CardioIA (Parte 2)

Este diretório contém o fluxo do Node-RED para consumir os dados publicados no tópico MQTT `cardioia/ana/v1/vitals` e exibir um dashboard com BPM (gráfico), temperatura (medidor) e status (texto/LED), além de nós de debug para inspeção.

## Requisitos
- Node.js instalado
- Node-RED (global)
- Paleta `node-red-dashboard`

## Instalação e execução
1. Instale o Node-RED globalmente:
   ```bash
   npm i -g node-red
   ```
2. Inicie o Node-RED:
   ```bash
   node-red
   ```
   O Node-RED geralmente abre em http://127.0.0.1:1880
3. Instale a paleta do dashboard:
   - No editor do Node-RED, vá em "Menu" (canto direito) → "Manage palette" → "Install"
   - Busque por `node-red-dashboard` e instale.

## Importar o fluxo
1. Copie o conteúdo do arquivo `apps/dashboard-nodered/flows.json` do repositório.
2. No Node-RED, clique em "Menu" → "Import" → cole o JSON → "Import".
3. Configure o broker MQTT do nó "MQTT In" para o HiveMQ Cloud (TLS 8883):
   - Clique no nó "Vitals MQTT" → campo "Server" → "Add new mqtt-broker…" → Configure:
     - Server: `185a5dc3e6434acf93f3c4da0ae7f24d.s1.eu.hivemq.cloud`
     - Port: `8883`
     - TLS: habilitado (pode usar uma config TLS sem verificação para fins de demo)
     - Client ID: `cardioia-nodered` (ou automático)
     - Username/Password: conforme suas credenciais do HiveMQ Cloud
   - Tópico já configurado: `cardioia/ana/v1/vitals`
4. Clique em "Deploy".

## O que o fluxo faz
- Recebe mensagens JSON do ESP32 no tópico `cardioia/ana/v1/vitals`.
- Converte para objeto (`json` node) e normaliza (`function`): extrai `{ts, temp, hum, bpm}` e define `status`:
  - `OK`
  - `ALTA_TEMP` se `temp > 38`
  - `TAQUICARDIA` se `bpm > 120`
  - `ALTA_TEMP+TAQUICARDIA` se ambos
- Envia para:
  - `ui_chart`: série de BPM (linha, janela de 10 minutos)
  - `ui_gauge`: medidor de Temperatura (°C)
  - `ui_text` + `ui_template`: status em texto e LED colorido
  - `debug`: nós de debug para inspeção (`raw mqtt` e `normalized`)

## Acessar o dashboard
- Após o deploy, acesse:
  - http://127.0.0.1:1880/ui
- Grupo: "Vitals" na aba "CardioIA"

## Dicas de teste
- Com o ESP32/Wokwi publicando:
  - Envie `ONLINE` no console do ESP32 para ativar WiFi/MQTT (TLS 8883).
  - Verifique no dashboard o gráfico de BPM, o gauge de Temp e o status.
- Sem ESP32, você pode publicar uma mensagem de teste, por exemplo usando um nó "inject" + "mqtt out":
  ```json
  {"ts": 123456, "temp": 38.4, "hum": 50.2, "bpm": 132, "connected": true}
  ```

## Estrutura
```
apps/dashboard-nodered/
├─ flows.json
└─ README.md
```
