# Edge AI Smartband — Detecção de Quedas com Inteligência Artificial Embarcada

> Trabalho de Conclusão de Curso — Desenvolvimento de uma Smartband com Inteligência Artificial Embarcada para Detecção de Quedas em Indivíduos com Comprometimento Cognitivo

---

## Visão Geral

Este projeto implementa um sistema completo de detecção de quedas em tempo real, executando um modelo de *machine learning* diretamente no microcontrolador (TinyML). O dispositivo monitora continuamente dados de um IMU de 6 eixos, classifica movimentos com uma rede neural embarcada e, ao confirmar uma queda, envia alertas via MQTT para um backend em Python que notifica cuidadores através do Firebase Cloud Messaging (FCM).

### Principais características

- **Inferência na borda** — modelo TensorFlow Lite compilado via Edge Impulse, rodando no ESP32-S3 sem nuvem
- **Máquina de estados em 3 estágios** — reduz falsos positivos combinando ML + detecção de impacto físico + confirmação de imobilidade
- **Multitarefa em tempo real** — quatro tarefas FreeRTOS distribuídas entre os dois núcleos do ESP32-S3
- **Notificações push** — integração com Firebase Cloud Messaging via backend Python
- **Banco de dados em tempo real** — status do dispositivo e eventos de queda registrados no Firebase RTDB

---

## Arquitetura do Sistema

```
┌─────────────────────────────────────────────────┐
│             M5Stack AtomS3 (ESP32-S3)            │
│                                                  │
│  taskSensor (Core 1)   taskPhysics (Core 1)      │
│  ─ Leitura IMU 20 Hz   ─ Amostragem contínua     │
│  ─ Filtro média móvel  ─ Fila de picos físicos   │
│          │                      │                │
│          ▼                      ▼                │
│  taskML (Core 0)                                 │
│  ─ Classificador TFLite (Edge Impulse)           │
│  ─ Máquina de estados: IDLE→SUSPECT→CONFIRMA     │
│          │                                       │
│          ▼                                       │
│  taskMQTT (Core 0)                               │
│  ─ WiFiManager + PubSubClient                    │
│  ─ Publica evento de queda em JSON               │
└─────────────────┬───────────────────────────────┘
                  │ MQTT
        ┌─────────▼─────────┐
        │   pythonping.py   │
        │  ─ Bridge MQTT    │
        │  ─ Firebase RTDB  │
        │  ─ FCM (push)     │
        └───────────────────┘
```

---

## Algoritmo de Detecção de Quedas

A detecção é feita em três estágios consecutivos para minimizar falsos positivos:

| Estágio | Condição de Avanço |
|---|---|
| **IDLE → SUSPECT** | Classificador ML com confiança ≥ 85% em pelo menos 1 das últimas 2 janelas |
| **SUSPECT → AGUARDA IMOBILIDADE** | Pico de aceleração ≥ 3,85 g **ou** (≥ 3,40 g + giroscópio ≥ 380°/s) |
| **CONFIRMA QUEDA** | Imobilidade por ≥ 800 ms (variação < 0,12 g e giroscópio < 20°/s) |

O modelo ML processa janelas de **1 segundo** (20 amostras × 50 ms) com 6 canais (ax, ay, az, gx, gy, gz), produzindo probabilidades para as classes `Normal` e `Queda`.

---

## Hardware

| Componente | Especificação |
|---|---|
| Microcontrolador | M5Stack AtomS3 (ESP32-S3, dual-core 240 MHz) |
| IMU | 6 eixos integrado — acelerômetro + giroscópio |
| Conectividade | Wi-Fi 802.11 b/g/n |
| Alimentação | USB-C / bateria externa |

---

## Estrutura do Repositório

```
├── src/
│   ├── main.cpp                              # Firmware principal (ESP32-S3)
│   ├── pythonping.py                         # Backend: MQTT → Firebase
│   ├── saveCSV.py                            # Coleta de dados para CSV
│   ├── graphs.py                             # Visualização dos dados do IMU
│   ├── firebase-adminsdk-credentials.example.json  # Template de credenciais
│   └── ei_v12/                               # Modelo Edge Impulse (TFLite)
│       ├── edge-impulse-sdk/                 # SDK de inferência
│       ├── model-parameters/                 # Metadados e variáveis do modelo
│       └── tflite-model/                     # Modelo compilado (EON Compiler)
├── platformio.ini                            # Configuração do PlatformIO
└── .gitignore
```

---

## Modelo de Machine Learning

O modelo foi treinado e exportado via **[Edge Impulse Studio](https://studio.edgeimpulse.com)**:

- **Entrada:** 120 floats (6 canais × 20 amostras a 20 Hz)
- **Saída:** 2 classes — `Normal` e `Queda`
- **Motor de inferência:** TensorFlow Lite Micro com otimizações ESP-NN (Espressif Neural Network)
- **Arena de memória:** ~11 KB
- **Compilado com:** EON Compiler para menor binário e maior velocidade

---

## Configuração e Uso

### 1. Firmware (ESP32-S3)

**Pré-requisitos:** [PlatformIO](https://platformio.org/)

```bash
# Edite o IP do broker MQTT em src/main.cpp
const char* mqtt_server = "SEU_IP_DO_BROKER";

# Compile e grave
pio run -e m5stack-atoms3 -t upload
```

A configuração do Wi-Fi é feita via **WiFiManager** — na primeira execução, o dispositivo cria um ponto de acesso para inserção das credenciais.

### 2. Backend Python

**Pré-requisitos:** Python 3.8+

```bash
pip install paho-mqtt firebase-admin
```

**Configuração:**

1. Gere uma service account no [Google Cloud Console](https://console.cloud.google.com/) para o seu projeto Firebase
2. Renomeie o arquivo para `firebase-adminsdk-credentials.json` e coloque em `src/`
3. Edite `src/pythonping.py`:

```python
RTDB_URL    = "https://SEU_PROJETO-default-rtdb.firebaseio.com/"
BROKER_HOST = "SEU_IP_DO_BROKER"
```

```bash
python src/pythonping.py
```

### 3. Coleta de dados (opcional)

```bash
# Edite o IP e o caminho do CSV em src/saveCSV.py
python src/saveCSV.py

# Visualize os dados coletados
python src/graphs.py
```

---

## Stack Tecnológica

| Camada | Tecnologia |
|---|---|
| Firmware | C++ · Arduino · FreeRTOS |
| ML Embarcado | TensorFlow Lite Micro · Edge Impulse |
| Comunicação | MQTT (PubSubClient) · Wi-Fi |
| Backend | Python · paho-mqtt · Firebase Admin SDK |
| Notificações | Firebase Cloud Messaging (FCM) |
| Banco de dados | Firebase Realtime Database |
| Build | PlatformIO |

---

## Dependências do Firmware

```ini
m5stack/M5Unified          # Abstração de hardware M5Stack
knolleary/PubSubClient     # Cliente MQTT
bblanchon/ArduinoJson      # Serialização JSON
tzapu/WiFiManager          # Configuração Wi-Fi automática
src/ei_v12                 # Modelo Edge Impulse (local)
```

---

## Licença

Projeto acadêmico desenvolvido como Trabalho de Conclusão de Curso.
