"""Bridge MQTT -> InfluxDB 2.x.

Assina o mesmo topico que o server_mqtt.py (/tcc/#) e grava cada leitura no
InfluxDB para visualizacao no Grafana. Roda em paralelo ao server_mqtt.py
(que continua salvando no SQLite) — os dois caminhos sao independentes.

Modelo de dados:
  measurement: consumo
  tags:        node (MAC do topico), papel (LEADER/MEMBER)
  fields:      current_ma, battery_pct

Config por variaveis de ambiente (com defaults para a bancada). O token e lido
de server/.influx_token (fora do git) ou da env INFLUX_TOKEN.
"""

import json
import os
from pathlib import Path

import paho.mqtt.client as mqtt
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS

# ---- Config ----
MQTT_HOST = os.environ.get("MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "1883"))
MQTT_TOPIC = "/tcc/#"

INFLUX_URL = os.environ.get("INFLUX_URL", "http://64.181.160.152:8086")
INFLUX_ORG = os.environ.get("INFLUX_ORG", "meu_tcc")
INFLUX_BUCKET = os.environ.get("INFLUX_BUCKET", "dados_esps")
MEASUREMENT = "consumo"

_TOKEN_FILE = Path(__file__).with_name(".influx_token")


def _load_token():
    tok = os.environ.get("INFLUX_TOKEN")
    if tok:
        return tok.strip()
    if _TOKEN_FILE.exists():
        return _TOKEN_FILE.read_text().strip()
    raise SystemExit(
        f"Token nao encontrado. Defina INFLUX_TOKEN ou crie {_TOKEN_FILE}.")


influx = InfluxDBClient(url=INFLUX_URL, token=_load_token(), org=INFLUX_ORG)
write_api = influx.write_api(write_options=SYNCHRONOUS)


def on_connect(client, userdata, flags, rc, properties=None):
    print(f"Conectado ao Broker ({MQTT_HOST}:{MQTT_PORT}). Codigo: {rc}")
    client.subscribe(MQTT_TOPIC)


def on_message(client, userdata, msg):
    try:
        dados = json.loads(msg.payload.decode("utf-8"))
        corrente = dados.get("current_ma")
        if corrente is None:
            return
        bateria = dados.get("battery_pct")
        papel = dados.get("role") or "UNKNOWN"
        policy = dados.get("policy") or "UNKNOWN"
        run = dados.get("run") or "UNKNOWN"
        node = msg.topic.rsplit("/", 1)[-1]

        point = (
            Point(MEASUREMENT)
            .tag("node", node)
            .tag("papel", papel)
            .tag("policy", policy)
            .tag("run", run)
            .field("current_ma", float(corrente))
        )
        if bateria is not None:
            point = point.field("battery_pct", float(bateria))

        write_api.write(bucket=INFLUX_BUCKET, org=INFLUX_ORG, record=point)
        print(f"[influx] {node} {papel} I={corrente}mA bat={bateria}%")
    except Exception as e:
        # Uma falha de escrita/parse nao pode derrubar o bridge.
        print(f"Erro no processamento: {e}")


def main():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(MQTT_HOST, MQTT_PORT, 60)
    try:
        client.loop_forever()
    finally:
        influx.close()


if __name__ == "__main__":
    main()
