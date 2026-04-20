import paho.mqtt.client as mqtt
import csv
import os
import time
import json

# ======= CONFIGURAÇÕES =======
mqtt_server = "YOUR_MQTT_BROKER_IP"
port = 1883
csv_file = "PATH/TO/YOUR/output.csv"

FIELDNAMES = ["t", "ax", "ay", "az", "gx", "gy", "gz"]
BUFFER = []
BUFFER_MAX = 100  # grava a cada 100 mensagens

# ======= CRIA ARQUIVO SE NÃO EXISTIR =======
if not os.path.exists(csv_file):
    with open(csv_file, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=FIELDNAMES, delimiter=';')
        writer.writeheader()

# ======= FUNÇÕES =======
def flush_buffer():
    """Escreve o conteúdo do buffer no CSV."""
    global BUFFER
    if BUFFER:
        with open(csv_file, 'a', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=FIELDNAMES, delimiter=';')
            writer.writerows(BUFFER)
        print(f"{len(BUFFER)} linhas gravadas no CSV.")
        BUFFER = []

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Conectado ao broker MQTT!")
        client.subscribe("dados")
    else:
        print(f"Falha ao conectar, código {rc}")

def on_message(client, userdata, msg):
    global BUFFER
    try:
        payload = msg.payload.decode()
        data = json.loads(payload)
        BUFFER.append({
            "t": data.get("t", 0),
            "ax": data.get("ax", 0),
            "ay": data.get("ay", 0),
            "az": data.get("az", 0),
            "gx": data.get("gx", 0),
            "gy": data.get("gy", 0),
            "gz": data.get("gz", 0)
        })
        if len(BUFFER) >= BUFFER_MAX:
            flush_buffer()
    except Exception as e:
        print("Erro ao processar:", e)

# ======= INICIALIZA MQTT =======
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

try:
    client.connect(mqtt_server, port)
except Exception as e:
    print("Erro ao conectar:", e)
    exit()

client.loop_start()

# ======= LOOP PRINCIPAL =======
try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("Encerrando...")
    flush_buffer()
    client.loop_stop()
    client.disconnect()
