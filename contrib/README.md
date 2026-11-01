# SX1302 LoRaWAN Gateway — Configuração Local

Este documento descreve o setup do gateway LoRaWAN SX1302 em modo **local**, com
captura de frames via **UDP listener em Python** e integração com **Node-RED**
(alternativa mais simples ao ChirpStack para uso residencial).

## Arquitetura

```
[Sensor(es) LoRaWAN] → [SX1302 via lora_pkt_fwd] → UDP 127.0.0.1:1700
                                                        ↓
                                          [Python UDP Listener]
                                          (recv PUSH_DATA → decode → print + MQTT publish)
                                                        ↓
                                          [Mosquitto MQTT Broker] → [Node-RED MQTT in]
                                                        ↓
                                             [function: decodificar payload]
                                                        ↓
                                    debug / dashboard / banco de dados / alertas
```

## Arquivos

### `contrib/systemd/`

| Arquivo | Descrição |
|---|---|
| `global_conf.json` | Configuração original — envia para TTN Cloud (`eu1.cloud.thethings.network`) |
| `local_conf.json` | Configuração local — server `127.0.0.1`, porta `1700` |
| `lora_pkt_fwd.service` | Systemd — forwarder com `global_conf.json` (TTN Cloud) |
| `lora_pkt_fwd_local.service` | Systemd — forwarder com `local_conf.json` (Local) |

### `contrib/python-udp-listener/`

| Arquivo | Descrição |
|---|---|
| `lora_pkt_fwd_listener.py` | UDP listener — escuta PUSH_DATA, decodifica frames e publica no MQTT |
| `lora_pkt_fwd_local_listener.service` | Systemd — roda o listener (unit path → `/opt/udp-listener/`) |
| `lora_gateway_rx.json` | Flow Node-RED (MQTT in → decode function → debug) |

## Como funciona o listener

- Escuta UDP em `127.0.0.1:1700`.
- Recebe datagrams no formato Semtech packet forwarder (PUSH_DATA, id `0x00`).
- Faz parse do JSON com array `rxpk`.
- Decodifica o campo `data` (Base64) → payload binário (fPort = 1º byte).
- Imprime as informações do frame: `time`, `freq`, `chan`, `modu`, `datr`, `rssi`, `lsnr`, `fPort`, etc.
- Publica no MQTT (tópico `lora/gateway/RX`) para integração com Node-RED.

> O listener **não** substitui um network server completo (sem ativações/downlinks);
> serve para capturar e inspecionar frames UPLINK recebidos do SX1302.

## Passo a passo (Raspberry Pi)

### 1. Copiar arquivos para o RPi

```bash
# Forwarder + config
sudo cp <repo>/contrib/systemd/local_conf.json /opt/sx1302_hal/
sudo cp <repo>/contrib/systemd/lora_pkt_fwd_local.service /etc/systemd/system/

# Listener
sudo mkdir -p /opt/udp-listener
sudo cp <repo>/contrib/python-udp-listener/lora_pkt_fwd_listener.py /opt/udp-listener/
sudo cp <repo>/contrib/python-udp-listener/lora_pkt_fwd_local_listener.service /etc/systemd/system/
sudo systemctl daemon-reload

# Se você tinha instalado a unit antiga (lora_pkt_fwd_listener.service), apague-a:
sudo rm -f /etc/systemd/system/lora_pkt_fwd_listener.service
sudo systemctl daemon-reload
```

### 2. Instalar dependências

```bash
# Mosquitto (broker MQTT)
sudo apt update
sudo apt install -y mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto

# Node-RED (script oficial)
bash <(curl -sL https://raw.githubusercontent.com/node-red/linux-installers/master/deb/update-nodejs-and-nodered)

# Dependência MQTT do listener (forma correta no Debian)
sudo apt install -y python3-paho-mqtt
```

### 3. Iniciar serviços

```bash
sudo systemctl enable --now lora_pkt_fwd_local
sudo systemctl enable --now lora_pkt_fwd_local_listener
sudo systemctl enable --now nodered
```

### 4. Importar flow no Node-RED

1. Acesse `http://phobos.local:1880`
2. **Menu (≡) → Import** → selecionar `lora_gateway_rx.json`
3. **Deploy**

O flow contém 5 nós em cadeia:

```
[MQTT in] → [function: Decodificar payload LoRa] → [debug]
```

A função decodifica o Base64 do campo `data` e parseia o formato
`T:25.3C,H:60.2%,N:1` em um objeto JSON:

```json
{
  "temperature": 25.3,
  "humidity": 60.2,
  "packet": 1,
  "rssi": -45,
  "snr": 8.5,
  "freq": 904.3,
  "time": "2026-11-01T20:00:00.000000Z"
}
```

Código da função (caso queira criar manualmente no Node-RED):

```javascript
const pkt = msg.payload;
if (!pkt || !pkt.data) { return null; }

const raw = Buffer.from(pkt.data, 'base64').toString('utf8');
msg.rawPayload = raw;

const result = {};
raw.split(',').forEach(part => {
    const [key, val] = part.split(':');
    const num = parseFloat(val);
    if (key === 'T') result.temperature = num;
    else if (key === 'H') result.humidity = num;
    else if (key === 'N') result.packet = parseInt(val);
});

msg.payload = result;
msg.rssi = pkt.rssi;
msg.snr = pkt.lsnr;
msg.freq = pkt.freq;
msg.time = pkt.time;

return msg;
```

### 5. Logs e verificação

```bash
# Logs do listener
sudo journalctl -u lora_pkt_fwd_local_listener -f

# Logs do Node-RED
sudo journalctl -u nodered -f

# Verificar serviços
for s in mosquitto lora_pkt_fwd_local lora_pkt_fwd_local_listener nodered; do
  echo -n "$s: "; systemctl is-active $s;
done
```

## Dashboard visual (Node-RED Dashboard 2.0)

> ⚠️ **Node-RED 5.0 usa Dashboard 2.0** (`@flowfuse/node-red-dashboard`), não o
> pacote legado `node-red-dashboard`. O pacote legado (3.6.6) é **incompatível**
> com Node-RED 5.0.

### Instalar o Dashboard 2.0

```bash
# No Raspberry Pi
cd /home/mododo/.node-red
npm install @flowfuse/node-red-dashboard
sudo systemctl restart nodered
```

### Criar o dashboard manualmente (recomendado)

O Dashboard 2.0 usa nós com **hífen** (`ui-gauge`, `ui-chart`, `ui-text`, `ui-page`,
`ui-group`), não com underscore (`ui_gauge`). Eles aparecem na paleta do Node-RED
na categoria **dashboard**.

1. Acesse `http://phobos.local:1880`
2. Na paleta esquerda, procure a categoria **dashboard**
3. Arraste para o canvas:
   - **ui-page** (cria uma página do dashboard)
   - **ui-group** (cria um grupo dentro da página)
   - **ui-gauge** (gauge de temperatura)
   - **ui-gauge** (gauge de umidade)
   - **ui-chart** (gráfico de histórico)
   - **ui-text** (info do pacote)
4. Conecte: **MQTT in** → **function (decode)** → [gauge_temp, gauge_hum, chart, text, debug]
5. Configure cada nó:
   - **ui-gauge (temp)**: Value = `{{msg.payload.temperature}}`, Min=0, Max=50, Units=°C
   - **ui-gauge (hum)**: Value = `{{msg.payload.humidity}}`, Min=0, Max=100, Units=%
   - **ui-chart**: Value = `{{msg.payload.temperature}}`, Label = Temperatura (°C)
   - **ui-text**: Value format = `RSSI: {{msg.payload.rssi}} dBm | SNR: {{msg.payload.snr}} | Pkt #{{msg.payload.packet}}`
6. **Deploy**
7. Acesse o dashboard em: `http://phobos.local:1880/dashboard`

### Como ficaria o dashboard

```
┌─────────────────────────────────────────────┐
│  LoRa Gateway Dashboard                     │
├──────────────────┬──────────────────────────┤
│  Sensores        │  Histórico               │
│  ┌────┐ ┌────┐  │  ┌────────────────────┐  │
│  │ 25 │ │ 60 │  │  │  ╱╲   ╱╲         │  │
│  │ °C │ │ %  │  │  │ ╱  ╲ ╱  ╲        │  │
│  └────┘ └────┘  │  └────────────────────┘  │
├──────────────────┴──────────────────────────┤
│  Info do Pacote                             │
│  RSSI: -45 dBm | SNR: 8.5 | Freq: 904.3   │
│  MHz | Pkt #1                              │
└─────────────────────────────────────────────┘
```

> O dashboard atualiza automaticamente a cada pacote recebido do ESP32.

## Teste com dispositivo ESP32 + SX1276

Para validar a recepção do gateway sem um network server completo, use o sketch de
exemplo incluído no repositório:

- `examples/sx1276esp32/sketch_sx1276esp32.ino` — sketch Arduino para Heltec WiFi LoRa 32 V2 (SX1276)
- `examples/sx1276esp32/README.md` — descrição da placa

### Parâmetros críticos do sketch (devem bater com o gateway)

| Parâmetro | Valor | Motivo |
|---|---|---|
| Frequência | 904.3 MHz | Bate com `chan_multiSF_2` do gateway (radio 0) |
| Spreading Factor | 7 | Habilitado no gateway (`chan_multiSF_All`) |
| Bandwidth | 125 kHz | Padrão do gateway |
| Coding Rate | 4/5 | Padrão LoRaWAN |
| Sync Word | `0x34` | Obrigatório — gateway SX1302 filtra por sync word LoRaWAN |
| CRC | habilitado | Gateway marca `stat=1` (CRC OK) |

O sketch envia dados simulados de sensores (temperatura, umidade) no formato
`T:25.3C,H:60.2%,N:1` e exibe os dados no display OLED.

## Persistência

### O que sobrevive a um desligamento/reboot

| Item | Persiste? | Onde fica |
|---|---|---|
| Serviços systemd (enabled) | ✅ Sim | `/etc/systemd/system/` |
| `local_conf.json` / `global_conf.json` | ✅ Sim | `/opt/sx1302_hal/` |
| Binário `lora_pkt_fwd` | ✅ Sim | `/opt/sx1302_hal/` |
| Script Python listener | ✅ Sim | `/opt/udp-listener/` |
| Flows do Node-RED | ✅ Sim | `/home/mododo/.node-red/flows.json` |
| Credenciais Node-RED | ✅ Sim | `/home/mododo/.node-red/flows_cred.json` |
| Config do Mosquitto | ✅ Sim | `/etc/mosquitto/` |
| Config do Node-RED | ✅ Sim | `/home/mododo/.node-red/settings.js` |

### Cuidados

1. **Node-RED**: os flows só são salvos ao clicar em **Deploy**.
2. **SD card**: desligue com `sudo shutdown -h now` (não puxar o cabo).
3. **Backup**: copie `/home/mododo/.node-red/flows.json` periodicamente.

## Observações importantes

- **Node-RED 5.0** usa **Dashboard 2.0** (`@flowfuse/node-red-dashboard`), não o
  pacote legado `node-red-dashboard`. O legado é incompatível com Node-RED 5.0.
- Os nós do Dashboard 2.0 usam **hífen** (`ui-gauge`, `ui-chart`, `ui-text`,
  `ui-page`, `ui-group`), não underscore (`ui_gauge`).
- A instalação do Dashboard 2.0 deve ser feita no **userDir** do Node-RED:
  `cd /home/mododo/.node-red && npm install @flowfuse/node-red-dashboard`.

## Commits relacionados

- `7812978` feat: systemd config
- `5151e43` feat: add local_conf.json for local LoRaWAN gateway operation
- `5f5f3ec` feat: add local systemd service unit for local packet forwarder
- `20dfd04` feat: add python UDP listener for local LoRaWAN gateway
- `92d8245` refactor: move udp listener files to contrib/python-udp-listener
- `f70bc5e` refactor: point udp listener service to /opt/udp-listener
- `78d0090` docs: add contrib README documenting local LoRaWAN setup
- `88834eb` refactor: rename listener service to lora_pkt_fwd_local_listener
- `88431f8` docs: add cleanup step for old listener service on RPi
- `9605c96` feat: add mqtt publishing to udp listener and node-red flow
- `9be2a09` fix: set mqtt broker host to 127.0.0.1 (mosquitto binds loopback)
- `28ecfb2` chore: replace flow with user's working node-red export from phobos
- `03e4ec9` feat: send sensor data via LoRa and display on OLED
- `3c2ae4a` docs: document payload decode function in node-red flow
- `6e30d8f` feat: add payload decode function node to node-red flow
- `421bce3` feat: add node-red dashboard flow and documentation
- `3bc722e` docs: install node-red-dashboard in userDir (fix unknown ui_* nodes)
- `5a6a166` docs: use apt install python3-paho-mqtt (Debian best practice)
- `133a71d` docs: recommend official node-red install script for RPi
- `2191724` refactor: remove node-red.service unit (use official nodered.service)
- `f6bec2d` fix: convert node-red flow to json array format
- `8570836` fix: remove invalid props from mqtt-broker config node (match node-red defaults)
- `eef7c14` feat: add mqtt publishing to udp listener and node-red flow
- `78d0090` docs: add contrib README documenting local LoRaWAN setup
- `92d8245` refactor: move udp listener files to contrib/python-udp-listener
- `20dfd04` feat: add python UDP listener for local LoRaWAN gateway
- `5f5f3ec` feat: add local systemd service unit for local packet forwarder
- `5151e43` feat: add local_conf.json for local LoRaWAN gateway operation
- `7812978` feat: systemd config
- `605a9c5` Add CHANGES.md documenting local modifications
- `b305e52` Merge pull request #1 from mrmododo/develop
- `418ba1f` Add backup of original util_chip_id reset_lgw.sh
- `b911263` Add pinctrl-based reset_lgw.sh to util_chip_id
- `9ebe6df` docs: reference sx1276esp32 example sketch in contrib README
- `03e4ec9` feat: send sensor data via LoRa and display on OLED
- `3c2ae4a` docs: document payload decode function in node-red flow
- `6e30d8f` feat: add payload decode function node to node-red flow
- `3a5c7e6` docs: clarify node-red 5 uses dashboard 2.0 (@flowfuse)
- `5a6a166` docs: use apt install python3-paho-mqtt (Debian best practice)
- `133a71d` docs: recommend official node-red install script for RPi
- `2191724` refactor: remove node-red.service unit (use official nodered.service)
- `f6bec2d` fix: convert node-red flow to json array format
- `8570836` fix: remove invalid props from mqtt-broker config node (match node-red defaults)
- `eef7c14` feat: add mqtt publishing to udp listener and node-red flow
- `78d0090` docs: add contrib README documenting local LoRaWAN setup
- `92d8245` refactor: move udp listener files to contrib/python-udp-listener
- `20dfd04` feat: add python UDP listener for local LoRaWAN gateway
- `5f5f3ec` feat: add local systemd service unit for local packet forwarder
- `5151e43` feat: add local_conf.json for local LoRaWAN gateway operation
- `7812978` feat: systemd config