import time
import json
import os
import paho.mqtt.client as mqtt

FCM_ENABLED = True
FCM_SERVICE_ACCOUNT_JSON = os.path.join(
    os.path.dirname(__file__),
    "firebase-adminsdk-credentials.json",  # rename your service account file to this
)
FCM_TOPIC = "device"

RTDB_ENABLED = True
RTDB_URL = "https://YOUR_PROJECT_ID-default-rtdb.firebaseio.com/"

RTDB_STATUS_PATH = "/sensores/status_conexao"
RTDB_FALL_PATH = "/sensores/queda"

firebase_status_ref = None
firebase_fall_ref = None

try:
    if FCM_ENABLED or RTDB_ENABLED:
        import firebase_admin
        from firebase_admin import credentials

        if FCM_ENABLED:
            from firebase_admin import messaging

        if RTDB_ENABLED:
            from firebase_admin import db

        _cred = credentials.Certificate(FCM_SERVICE_ACCOUNT_JSON)

        firebase_admin.initialize_app(_cred, {
            "databaseURL": RTDB_URL
        })
        print("[Firebase Admin] inicializado (FCM/RTDB).")

        if RTDB_ENABLED:
            firebase_status_ref = db.reference(RTDB_STATUS_PATH)
            firebase_fall_ref = db.reference(RTDB_FALL_PATH)

except Exception as e:
    print("[Firebase Admin] Falha ao iniciar. Erro:", repr(e))
    FCM_ENABLED = False
    RTDB_ENABLED = False
    firebase_status_ref = None
    firebase_fall_ref = None


BROKER_HOST = "YOUR_MQTT_BROKER_IP"
BROKER_PORT = 1883

STATUS_TOPIC = "device/esp32_01/status"
FALL_TOPIC   = "device/esp32_01/truefall"
EVENTS_TOPIC = "device/esp32_01/events"

state = {
    "rtdb_status": None,
    "disconnect_alert_sent": False,
}


def set_rtdb_status(new_status: str):
    if not RTDB_ENABLED or firebase_status_ref is None:
        return

    if state.get("rtdb_status") == new_status:
        return

    try:
        firebase_status_ref.set(new_status)
        state["rtdb_status"] = new_status
        print(f"[RTDB] {RTDB_STATUS_PATH} = {new_status}")
    except Exception as e:
        print("[RTDB] erro ao atualizar status:", repr(e))


def set_rtdb_fall(new_value: int):
    if not RTDB_ENABLED or firebase_fall_ref is None:
        return

    try:
        firebase_fall_ref.set(new_value)
        print(f"[RTDB] {RTDB_FALL_PATH} = {new_value}")
    except Exception as e:
        print("[RTDB] erro ao atualizar queda:", repr(e))


def send_fcm_event(ev_type: str, reason: str, at_ts: int):
    if not FCM_ENABLED:
        return

    data = {
        "type": ev_type,
        "reason": reason,
        "at": str(at_ts)
    }

    msg = messaging.Message(
        topic=FCM_TOPIC,
        data=data,
        notification=messaging.Notification(title=ev_type, body=reason),
        android=messaging.AndroidConfig(priority="high"),
        apns=messaging.APNSConfig(headers={"apns-priority": "10"})
    )

    try:
        msg_id = messaging.send(msg)
        print("[FCM] sent:", msg_id, "payload:", data)
    except Exception as e:
        print("[FCM] send error:", repr(e), "payload:", data)


def publish_event(client, ev_type, reason):
    at_ts = int(time.time())
    payload = {"type": ev_type, "reason": reason, "at": at_ts}

    try:
        client.publish(EVENTS_TOPIC, json.dumps(payload), qos=1, retain=False)
        print("EVENT(MQTT):", payload)
    except Exception as e:
        print("[MQTT] erro ao publicar evento:", repr(e), "payload:", payload)

    send_fcm_event(ev_type, reason, at_ts)


def mark_connected():
    set_rtdb_status("Conectado")
    state["disconnect_alert_sent"] = False


def mark_disconnected():
    set_rtdb_status("Desconectado")


def on_connect(client, userdata, flags, rc):
    print("[MQTT] Connected rc=", rc)
    client.subscribe([
        (STATUS_TOPIC, 1),
        (FALL_TOPIC, 1),
    ])
    print(f"[MQTT] inscrito em: {STATUS_TOPIC}, {FALL_TOPIC}")


def on_disconnect(client, userdata, rc):
    print("[MQTT] desconectado do broker rc=", rc)


def on_message(client, userdata, msg):
    topic = msg.topic
    raw_payload = msg.payload.decode("utf-8", errors="ignore").strip()
    payload = raw_payload.lower()

    print(f"[MQTT] topic={topic} retain={msg.retain} payload={raw_payload}")

    if topic == STATUS_TOPIC:
        if payload == "offline":
            was_disconnected = (state.get("rtdb_status") == "Desconectado")

            mark_disconnected()

            if not was_disconnected and not state["disconnect_alert_sent"]:
                publish_event(
                    client,
                    "ALERTA DE DESCONEXAO",
                    "Usuário desconectado, verifique a situação do portador"
                )
                state["disconnect_alert_sent"] = True

        elif payload == "online":
            mark_connected()

        else:
            print(f"[MQTT STATUS] payload ignorado: {raw_payload}")

    elif topic == FALL_TOPIC:
        if msg.retain:
            print("[MQTT FALL] mensagem retida ignorada:", raw_payload)
            return

        print("[MQTT FALL] payload:", raw_payload)
        set_rtdb_fall(1)
        send_fcm_event(
            ev_type="QUEDA DETECTADA",
            reason="Uma queda foi detectada pelo dispositivo",
            at_ts=int(time.time())
        )


def main():
    client = mqtt.Client(client_id="geofence_backend_esp32_01", clean_session=True)
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message

    print(f"[MQTT] conectando em {BROKER_HOST}:{BROKER_PORT} ...")
    client.connect(BROKER_HOST, BROKER_PORT, keepalive=30)
    client.loop_forever()


if __name__ == "__main__":
    main()