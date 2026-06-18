import paho.mqtt.client as mqtt
import sqlite3
import json

DB_NAME = "sensores_iiot.db"

def init_db():
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()
    # Adicionando colunas de energia para análise do FND
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS leituras (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            topico TEXT,
            temperatura REAL,
            corrente_ma REAL,
            bateria_pct REAL,
            papel TEXT,
            measured_time TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    # Migracao: adiciona colunas em bancos ja existentes (CREATE TABLE
    # IF NOT EXISTS nao altera tabela ja criada).
    for col in ("measured_time TEXT", "papel TEXT"):
        try:
            cursor.execute(f"ALTER TABLE leituras ADD COLUMN {col}")
        except sqlite3.OperationalError:
            pass  # coluna ja existe
    conn.commit()
    conn.close()

def on_connect(client, userdata, flags, rc, properties=None):
    print(f"Conectado ao Broker (127.0.0.1). Codigo: {rc}")
    client.subscribe("tcc/#")
    client.subscribe("/tcc/#")

def on_message(client, userdata, msg):
    payload = msg.payload.decode('utf-8')
    try:
        dados = json.loads(payload)
        
        # Extraindo dados conforme o snprintf do seu firmware
        temp = dados.get("temperature")
        corrente = dados.get("current_ma")
        bateria = dados.get("battery_pct")
        papel = dados.get("role")
        measured_time = dados.get("measured_time")

        if temp is not None or corrente is not None:
            conn = sqlite3.connect(DB_NAME)
            cursor = conn.cursor()
            cursor.execute('''
                INSERT INTO leituras (topico, temperatura, corrente_ma, bateria_pct, papel, measured_time)
                VALUES (?, ?, ?, ?, ?, ?)
            ''', (msg.topic, temp, corrente, bateria, papel, measured_time))
            conn.commit()
            conn.close()
            print(f"[{msg.topic}] Salvo: {papel} | Temp={temp}C | I={corrente}mA | Bat={bateria}% | t={measured_time}")
            
    except Exception as e:
        print(f"Erro no processamento: {e}")

init_db()
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_message = on_message

# Broker mosquitto roda localmente neste notebook (mesma rede "samu" dos ESPs).
# 127.0.0.1 e robusto a troca de IP por DHCP. Os ESPs publicam via MQTT_BROKER_URI
# (sdkconfig) apontando para o IP deste host na rede "samu".
client.connect("127.0.0.1", 1883, 60)
client.loop_forever()
