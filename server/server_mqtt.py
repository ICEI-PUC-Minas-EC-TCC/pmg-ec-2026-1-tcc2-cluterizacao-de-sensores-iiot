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
            measured_time TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    # Migracao: adiciona measured_time em bancos ja existentes (CREATE TABLE
    # IF NOT EXISTS nao altera tabela ja criada).
    try:
        cursor.execute("ALTER TABLE leituras ADD COLUMN measured_time TEXT")
    except sqlite3.OperationalError:
        pass  # coluna ja existe
    conn.commit()
    conn.close()

def on_connect(client, userdata, flags, rc, properties=None):
    print(f"Conectado ao Broker (192.168.18.150). Codigo: {rc}")
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
        measured_time = dados.get("measured_time")

        if temp is not None or corrente is not None:
            conn = sqlite3.connect(DB_NAME)
            cursor = conn.cursor()
            cursor.execute('''
                INSERT INTO leituras (topico, temperatura, corrente_ma, bateria_pct, measured_time)
                VALUES (?, ?, ?, ?, ?)
            ''', (msg.topic, temp, corrente, bateria, measured_time))
            conn.commit()
            conn.close()
            print(f"[{msg.topic}] Salvo: Temp={temp}C | I={corrente}mA | Bat={bateria}% | t={measured_time}")
            
    except Exception as e:
        print(f"Erro no processamento: {e}")

init_db()
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_message = on_message

# Use o IP que encontramos no seu ip addr
#client.connect("192.168.18.150", 1883, 60)
#client.loop_forever()

client.connect("10.245.197.185", 1883, 60)
client.loop_forever()
