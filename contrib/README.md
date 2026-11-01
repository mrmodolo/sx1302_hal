# SX1302 LoRaWAN Gateway — Configuração Local

Este documento descreve o setup do gateway LoRaWAN SX1302 em modo **local**, com
captura de frames via **UDP listener em Python** (alternativa mais simples ao
ChirpStack para uso residencial).

## Arquitetura

```
[Sensor(es) LoRaWAN] → [SX1302 via lora_pkt_fwd] → UDP 127.0.0.1:1700
                                                        ↓
                                          [Python UDP Listener]
                                                        ↓
                                imprime frame + fPort + payload (Base64)
```

- O `lora_pkt_fwd` (packet forwarder Semtech) faz a demodulação e envia os frames
  LoRaWAN via UDP (protocolo PUSH_DATA) para o server configurado em
  `server_address:serv_port_up`.
- No modo local, o `local_conf.json` aponta para `127.0.0.1:1700`.
- O script Python escuta nessa porta e decodifica o JSON/PUSH_DATA.

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
| `lora_pkt_fwd_listener.py` | UDP listener — escuta PUSH_DATA e decodifica frames |
| `lora_pkt_fwd_local_listener.service` | Systemd — roda o listener (unit path → `/opt/udp-listener/`) |

## Como funciona o listener

- Escuta UDP em `127.0.0.1:1700`.
- Recebe datagrams no formato Semtech packet forwarder (PUSH_DATA, id `0x00`).
- Faz parse do JSON com array `rxpk`.
- Decodifica o campo `data` (Base64) → payload binário (fPort = 1º byte).
- Imprime as informações do frame: `time`, `freq`, `chan`, `modu`, `datr`, `rssi`, `lsnr`, `fPort`, etc.

> O listener **não** substitui um network server completo (sem ativações/downlinks);
> serve para capturar e inspecionar frames UPLINK recebidos do SX1302.

## Passo a passo (Raspberry Pi)

```bash
# Copiar listener
sudo mkdir -p /opt/udp-listener
sudo cp <repo>/contrib/python-udp-listener/lora_pkt_fwd_listener.py /opt/udp-listener/
sudo cp <repo>/contrib/python-udp-listener/lora_pkt_fwd_local_listener.service /etc/systemd/system/
sudo systemctl daemon-reload

# Se você tinha instalado a unit antiga (lora_pkt_fwd_listener.service), apague-a:
sudo rm -f /etc/systemd/system/lora_pkt_fwd_listener.service
sudo systemctl daemon-reload

# Reiniciar forwarder + listener
sudo systemctl restart lora_pkt_fwd_local
sudo systemctl restart lora_pkt_fwd_local_listener

# Habilitar listener pro boot
sudo systemctl enable lora_pkt_fwd_local_listener

# Logs
sudo journalctl -u lora_pkt_fwd_local_listener -f
```

## Integração com Node-RED

O Node-RED não possui nó UDP nativo no core; o caminho mais simples para levar os
frames UDP do SX1302 até o Node-RED é via **MQTT**.

Arquitetura atualizada:

```
[Sensor(es) LoRaWAN] → [SX1302 via lora_pkt_fwd] → UDP 127.0.0.1:1700
                                                        ↓
                                          [Python UDP Listener]
                                          (publica no MQTT: lora/gateway/RX)
                                                        ↓
                                          [Mosquitto MQTT Broker] → [Node-RED MQTT in]
```

### Arquivos relacionados

- `contrib/python-udp-listener/lora_pkt_fwd_listener.py` — listener UDP que também publica no MQTT (`MQTT_HOST`, `MQTT_PORT`, `MQTT_TOPIC`).
- `contrib/python-udp-listener/lora_gateway_rx.json` — flow exemplo do Node-RED (MQTT in → debug).

### Passo a passo no Raspberry Pi

```bash
# 1. Instalar Mosquitto (broker MQTT)
sudo apt update
sudo apt install -y mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto

# 2. Instalar Node-RED
#    Recomendado: script oficial do Node-RED para RPi/Debian
bash <(curl -sL https://raw.githubusercontent.com/node-red/linux-installers/master/deb/update-nodejs-and-nodered)

#    (alternativa manual, se preferir)
# curl -sL https://deb.nodesource.com/setup_20.x | sudo -E bash -
# sudo apt install -y nodejs
# sudo npm install -g node-red

# Observacao: o script oficial cria o servico "nodered" (sem hifen).
#   sudo systemctl enable --now nodered
#   sudo journalctl -u nodered -f

# 3. Instalar dependência MQTT do listener
sudo apt install -y python3-paho-mqtt

# 4. Reiniciar listener (publica no MQTT)
sudo systemctl restart lora_pkt_fwd_local_listener

# 5. Subir Node-RED e importar o flow
#    - Acessar http://<ip-do-raspberry>:1880
#    - Menu > Import > colar/importar o conteúdo de lora_gateway_rx.json
#    - Deploy

O flow importado contém 5 nós em cadeia:

    [MQTT in] → [function: Decodificar payload LoRa] → [debug]

A função decodifica o Base64 do campo "data" e parseia o formato
"T:25.3C,H:60.2%,N:1" em um objeto JSON:

    {
      "temperature": 25.3,
      "humidity": 60.2,
      "packet": 1,
      "rssi": -45,
      "snr": 8.5,
      "freq": 904.3,
      "time": "2026-11-01T20:00:00.000000Z"
    }

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

O Node-RED admin fica disponível em `http://<ip-do-raspberry>:1880`.
```

## Dashboard visual (Node-RED Dashboard)

Para visualizar os dados dos sensores em tempo real, instale o pacote de dashboard:

```bash
# No Raspberry Pi
cd /home/modolo/.node-red
npm install node-red-dashboard
sudo systemctl restart nodered
```

> ⚠️ Instale no **userDir** (`/home/modolo/.node-red`), não globalmente.
> O Node-RED (rodando como usuário `modolo`) só encontra os nós
> instalados no seu próprio `node_modules`.

### Verificar se instalou

```bash
ls /home/modolo/.node-red/node_modules/node-red-dashboard
```

Se a pasta existir, o Node-RED carregará os nós `ui_*` automaticamente.

### Como usar

1. Acesse `http://phobos.local:1880`
2. **Menu (≡) → Import** → selecionar `lora_dashboard.json`
3. **Deploy**
4. Acesse o dashboard em: `http://phobos.local:1880/ui`

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

> ⚠️ Se o Sync Word for diferente de `0x34`, o gateway **não recebe** o frame.

## Commits relacionados

- `7812978` feat: systemd config
- `5151e43` feat: add local_conf.json for local LoRaWAN gateway operation
- `5f5f3ec` feat: add local systemd service unit for local packet forwarder
- `20dfd04` feat: add python UDP listener for local LoRaWAN gateway
- `92d8245` refactor: move udp listener files to contrib/python-udp-listener
- `f70bc5e` refactor: point udp listener service to /opt/udp-listener
