#!/usr/bin/env python3
"""
SX1302 LoRaWAN UDP Listener
==========================

Escuta datagrams UDP no protocolo Semtech packet forwarder (PUSH_DATA) enviados
pelo lora_pkt_fwd, decodifica os frames LoRaWAN (Base64 -> binario) e, opcionalmente,
publica no MQTT para integracao com Node-RED.

O lora_pkt_fwd (configurado em global_conf.json / local_conf.json) envia os
frames recebidos para o server_address:serv_port_up (nesse setup: 127.0.0.1:1700).

Este listener NAO substitui um network server completo (nao processa
ativacoes / nao gera downlinks): serve para capturar e inspecionar os
frames UPLINK recebidos do SX1302.
"""

import os
import socket
import json
import base64

LISTEN_HOST = "127.0.0.1"
LISTEN_PORT = 1700
PUSH_DATA_ID = 0x00

MQTT_HOST = os.environ.get("MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "1883"))
MQTT_TOPIC = os.environ.get("MQTT_TOPIC", "lora/gateway/RX")

try:
    import paho.mqtt.client as mqtt

    _mqtt_client = mqtt.Client()
    _mqtt_client.connect(MQTT_HOST, MQTT_PORT, 60)
    _mqtt_client.loop_start()
    MQTT_ENABLED = True
except Exception:
    _mqtt_client = None
    MQTT_ENABLED = False


def publish(pkt: dict) -> None:
    if not MQTT_ENABLED:
        return
    try:
        _mqtt_client.publish(MQTT_TOPIC, json.dumps(pkt), qos=0)
    except Exception as e:
        print(f"[ERRO] MQTT publish failed: {e}", flush=True)


def handle_push_data(data: bytes) -> None:
    if len(data) < 12:
        print("[ERRO] Datagrama UDP muito curto", flush=True)
        return

    version = data[0]
    token = data[1:3].hex()
    msg_id = data[3]
    gw_mac = data[4:12].hex()
    json_str = data[12:].decode("utf-8", errors="replace")

    if msg_id != PUSH_DATA_ID:
        # Ignora outros tipos (PUSH_ACK, PULL_DATA, PULL_RESP, TX_ACK...)
        return

    try:
        obj = json.loads(json_str)
    except Exception as e:
        print(f"[ERRO] Falha ao decodificar JSON: {e} | {json_str}", flush=True)
        return

    rxpk = obj.get("rxpk")
    if not rxpk:
        print(f"[STAT] status gateway: {obj.get('stat')}", flush=True)
        return

    for pkt in rxpk:
        b64data = pkt.get("data", "")
        raw = base64.b64decode(b64data) if b64data else b""
        fport = raw[0] if len(raw) > 0 else None
        print(
            f"[RX] time={pkt.get('time')} freq={pkt.get('freq')}MHz "
            f"chan={pkt.get('chan')} rfch={pkt.get('rfch')} modu={pkt.get('modu')} "
            f"datr={pkt.get('datr')} codr={pkt.get('codr')} stat={pkt.get('stat')} "
            f"rssi={pkt.get('rssi')} lsnr={pkt.get('lsnr')} size={pkt.get('size')} "
            f"fPort={fport} b64={b64data}",
            flush=True,
        )
        publish(pkt)


def main() -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((LISTEN_HOST, LISTEN_PORT))
    print(
        f"[LISTENER] UDP listener at {LISTEN_HOST}:{LISTEN_PORT} ready "
        f"(MQTT={'ON' if MQTT_ENABLED else 'OFF'})",
        flush=True,
    )
    while True:
        try:
            data, addr = sock.recvfrom(65535)
        except OSError:
            break
        handle_push_data(data)


if __name__ == "__main__":
    main()
