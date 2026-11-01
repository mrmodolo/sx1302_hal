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
| `lora_pkt_fwd_listener.service` | Systemd — roda o listener (unit path → `/opt/udp-listener/`) |

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

# Reiniciar forwarder + listener
sudo systemctl restart lora_pkt_fwd_local
sudo systemctl restart lora_pkt_fwd_local_listener

# Habilitar listener pro boot
sudo systemctl enable lora_pkt_fwd_local_listener

# Logs
sudo journalctl -u lora_pkt_fwd_local_listener -f
```

## Commits relacionados

- `7812978` feat: systemd config
- `5151e43` feat: add local_conf.json for local LoRaWAN gateway operation
- `5f5f3ec` feat: add local systemd service unit for local packet forwarder
- `20dfd04` feat: add python UDP listener for local LoRaWAN gateway
- `92d8245` refactor: move udp listener files to contrib/python-udp-listener
- `f70bc5e` refactor: point udp listener service to /opt/udp-listener
