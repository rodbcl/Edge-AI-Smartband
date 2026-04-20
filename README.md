<div align="center">

**Language / Idioma:**
[🇺🇸 English](#english) · [🇧🇷 Português](#português)

</div>

---

<a name="english"></a>

# Edge AI Smartband — Embedded Artificial Intelligence for Fall Detection

> **Final Thesis Project (TCC)** — Development of a Smartband with Embedded Artificial Intelligence for Fall Detection in Individuals with Cognitive Impairment

<div align="center">

**Authors:** Gabriel Cortes · Leonardo Zucco · Rodrigo Baldissera

</div>

---

## Overview

This project implements a complete real-time fall detection system that runs a machine learning model directly on the microcontroller (TinyML). The device continuously monitors a 6-axis IMU, classifies movements using an embedded neural network, and upon confirming a fall, sends alerts via MQTT to a Python backend that notifies caregivers through Firebase Cloud Messaging (FCM).

### Key Features

- **On-device inference** — TFLite model compiled via Edge Impulse, running on the ESP32-S3 without cloud dependency
- **3-stage state machine** — reduces false positives by combining ML + physical impact detection + immobility confirmation
- **Real-time multitasking** — four FreeRTOS tasks distributed across the two ESP32-S3 cores
- **Push notifications** — Firebase Cloud Messaging integration via Python backend
- **Real-time database** — device status and fall events stored in Firebase RTDB

---

## System Architecture

```
┌─────────────────────────────────────────────────┐
│             M5Stack AtomS3 (ESP32-S3)            │
│                                                  │
│  taskSensor (Core 1)   taskPhysics (Core 1)      │
│  - IMU reading @ 20 Hz  - Continuous sampling    │
│  - Moving avg filter    - Physics peak queue     │
│          │                      │                │
│          ▼                      ▼                │
│  taskML (Core 0)                                 │
│  - TFLite Classifier (Edge Impulse)              │
│  - State machine: IDLE → SUSPECT → CONFIRMED     │
│          │                                       │
│          ▼                                       │
│  taskMQTT (Core 0)                               │
│  - WiFiManager + PubSubClient                    │
│  - Publishes fall event as JSON                  │
└─────────────────┬───────────────────────────────┘
                  │ MQTT
        ┌─────────▼─────────┐
        │   pythonping.py   │
        │  - MQTT bridge    │
        │  - Firebase RTDB  │
        │  - FCM (push)     │
        └───────────────────┘
```

---

## Fall Detection Algorithm

Detection is performed in three consecutive stages to minimize false positives:

| Stage | Advancement Condition |
|---|---|
| **IDLE → SUSPECT** | ML classifier confidence >= 85% in at least 1 of the last 2 windows |
| **SUSPECT → WAIT IMMOBILITY** | Acceleration peak >= 3.85 g **or** (>= 3.40 g + gyroscope >= 380 deg/s) |
| **CONFIRMED FALL** | Immobility for >= 800 ms (variance < 0.12 g and gyroscope < 20 deg/s) |

The ML model processes **1-second windows** (20 samples x 50 ms) with 6 channels (ax, ay, az, gx, gy, gz), producing probabilities for the `Normal` and `Fall` classes.

---

## Hardware

| Component | Specification |
|---|---|
| Microcontroller | M5Stack AtomS3 (ESP32-S3, dual-core 240 MHz) |
| IMU | Built-in 6-axis — accelerometer + gyroscope |
| Connectivity | Wi-Fi 802.11 b/g/n |
| Power | USB-C / external battery |

---

## Repository Structure

```
├── src/
│   ├── main.cpp                              # Main firmware (ESP32-S3)
│   ├── pythonping.py                         # Backend: MQTT → Firebase
│   ├── saveCSV.py                            # Data collection to CSV
│   ├── graphs.py                             # IMU data visualization
│   ├── firebase-adminsdk-credentials.example.json  # Credentials template
│   └── ei_v12/                               # Edge Impulse model (TFLite)
│       ├── edge-impulse-sdk/                 # Inference SDK
│       ├── model-parameters/                 # Model metadata and variables
│       └── tflite-model/                     # Compiled model (EON Compiler)
├── platformio.ini                            # PlatformIO configuration
└── .gitignore
```

---

## Machine Learning Model

The model was trained and exported via **[Edge Impulse Studio](https://studio.edgeimpulse.com)**:

- **Input:** 120 floats (6 channels x 20 samples at 20 Hz)
- **Output:** 2 classes — `Normal` and `Fall`
- **Inference engine:** TensorFlow Lite Micro with ESP-NN optimizations (Espressif Neural Network)
- **Memory arena:** ~11 KB
- **Compiled with:** EON Compiler for smaller binary and faster execution

---

## Setup & Usage

### 1. Firmware (ESP32-S3)

**Prerequisites:** [PlatformIO](https://platformio.org/)

```bash
# Edit the MQTT broker IP in src/main.cpp
const char* mqtt_server = "YOUR_MQTT_BROKER_IP";

# Compile and flash
pio run -e m5stack-atoms3 -t upload
```

Wi-Fi credentials are configured via **WiFiManager** — on first boot, the device creates an access point for credential input.

### 2. Python Backend

**Prerequisites:** Python 3.8+

```bash
pip install paho-mqtt firebase-admin
```

**Configuration:**

1. Generate a service account in the [Google Cloud Console](https://console.cloud.google.com/) for your Firebase project
2. Rename the file to `firebase-adminsdk-credentials.json` and place it in `src/`
3. Edit `src/pythonping.py`:

```python
RTDB_URL    = "https://YOUR_PROJECT-default-rtdb.firebaseio.com/"
BROKER_HOST = "YOUR_MQTT_BROKER_IP"
```

```bash
python src/pythonping.py
```

### 3. Data Collection (optional)

```bash
# Edit the IP and CSV path in src/saveCSV.py
python src/saveCSV.py

# Visualize collected data
python src/graphs.py
```

---

## Tech Stack

| Layer | Technology |
|---|---|
| Firmware | C++ · Arduino · FreeRTOS |
| Embedded ML | TensorFlow Lite Micro · Edge Impulse |
| Communication | MQTT (PubSubClient) · Wi-Fi |
| Backend | Python · paho-mqtt · Firebase Admin SDK |
| Notifications | Firebase Cloud Messaging (FCM) |
| Database | Firebase Realtime Database |
| Build System | PlatformIO |

---

## Firmware Dependencies

```ini
m5stack/M5Unified          # M5Stack hardware abstraction
knolleary/PubSubClient     # MQTT client
bblanchon/ArduinoJson      # JSON serialization
tzapu/WiFiManager          # Automatic Wi-Fi configuration
src/ei_v12                 # Edge Impulse model (local)
```

---

<br><br>

---

<a name="português"></a>

# Edge AI Smartband — Inteligência Artificial Embarcada para Detecção de Quedas

> **Trabalho de Conclusão de Curso (TCC)** — Desenvolvimento de uma Smartband com Inteligência Artificial Embarcada para Detecção de Quedas em Indivíduos com Comprometimento Cognitivo

<div align="center">

**Autores:** Gabriel Cortes · Leonardo Zucco · Rodrigo Baldissera

</div>

---

## Visão Geral

Este projeto implementa um sistema completo de detecção de quedas em tempo real, executando um modelo de *machine learning* diretamente no microcontrolador (TinyML). O dispositivo monitora continuamente dados de um IMU de 6 eixos, classifica movimentos com uma rede neural embarcada e, ao confirmar uma queda, envia alertas via MQTT para um backend em Python que notifica cuidadores através do Firebase Cloud Messaging (FCM).

### Principais Características

- **Inferência na borda** — modelo TensorFlow Lite compilado via Edge Impulse, rodando no ESP32-S3 sem dependência de nuvem
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
│  - Leitura IMU 20 Hz   - Amostragem contínua     │
│  - Filtro média móvel  - Fila de picos físicos   │
│          │                      │                │
│          ▼                      ▼                │
│  taskML (Core 0)                                 │
│  - Classificador TFLite (Edge Impulse)           │
│  - Máquina de estados: IDLE → SUSPEITO → CONFIRMA│
│          │                                       │
│          ▼                                       │
│  taskMQTT (Core 0)                               │
│  - WiFiManager + PubSubClient                    │
│  - Publica evento de queda em JSON               │
└─────────────────┬───────────────────────────────┘
                  │ MQTT
        ┌─────────▼─────────┐
        │   pythonping.py   │
        │  - Bridge MQTT    │
        │  - Firebase RTDB  │
        │  - FCM (push)     │
        └───────────────────┘
```

---

## Algoritmo de Detecção de Quedas

A detecção é feita em três estágios consecutivos para minimizar falsos positivos:

| Estágio | Condição de Avanço |
|---|---|
| **IDLE → SUSPEITO** | Classificador ML com confiança >= 85% em pelo menos 1 das últimas 2 janelas |
| **SUSPEITO → AGUARDA IMOBILIDADE** | Pico de aceleração >= 3,85 g **ou** (>= 3,40 g + giroscópio >= 380°/s) |
| **QUEDA CONFIRMADA** | Imobilidade por >= 800 ms (variação < 0,12 g e giroscópio < 20°/s) |

O modelo ML processa janelas de **1 segundo** (20 amostras x 50 ms) com 6 canais (ax, ay, az, gx, gy, gz), produzindo probabilidades para as classes `Normal` e `Queda`.

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

- **Entrada:** 120 floats (6 canais x 20 amostras a 20 Hz)
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

### 3. Coleta de Dados (opcional)

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
