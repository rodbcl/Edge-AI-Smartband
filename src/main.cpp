#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <ctype.h>

// ==== Edge Impulse ====
#include "model-parameters/model_metadata.h"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

#define N 5 // média móvel

const char* mqtt_server = "YOUR_MQTT_BROKER_IP";

const char* willTopic   = "device/esp32_01/status";
const char* willMessage = "offline";
const char* fallTopic   = "device/esp32_01/truefall";

WiFiClient espClient;
PubSubClient client(espClient);

SemaphoreHandle_t xSemaphoreBufferFull;
SemaphoreHandle_t xSemaphoreBufferFree;

TaskHandle_t gTaskSensorHandle = nullptr;
TaskHandle_t gTaskMLHandle = nullptr;
TaskHandle_t gTaskMQTTHandle = nullptr;
TaskHandle_t gTaskPhysicsHandle = nullptr;

volatile uint32_t gLastInferenceTotalMs = 0;
volatile uint32_t gLastInferenceDspMs = 0;
volatile uint32_t gLastInferenceClassMs = 0;
volatile uint32_t gLastInferencePostMs = 0;
volatile uint32_t gInferenceCount = 0;
volatile uint32_t gInferenceTotalAccumMs = 0;

// ====== BUFFER DO MODELO ======
static float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
volatile size_t feat_ix = 0;

int ei_get_feature_data(size_t offset, size_t length, float *out_ptr) {
    memcpy(out_ptr, features + offset, length * sizeof(float));
    return 0;
}

// ====== BUFFERS MÉDIA MÓVEL ======
static float ax_buf[N], ay_buf[N], az_buf[N];
static float gx_buf[N], gy_buf[N], gz_buf[N];
static int mm_idx = 0;

static float media_movel(const float *b) {
    float s = 0.0f;
    for (int i = 0; i < N; i++) s += b[i];
    return s / (float)N;
}

// ============================================================================
// 2-factor check: thresholds e lógica (K de M) + impacto + imobilidade
// ============================================================================
#define ML_WIN_M 2
#define ML_WIN_K 1
static const float T_HIGH       = 0.85f;      // ajuste fino (ex: 0.85~0.95)
static const float IMPACT_G     = 3.85f;      // impacto em "g" (ajuste com seus logs)
static const float IMPACT_G_MID = 3.40f;      // impacto mais leve em "g" (ajuste com seus logs)
static const float IMPACT_GYRO  = 380.0f;     // GYRO FEATURE: threshold de pico angular para confirmar impacto durante o estado SUSPECT
static const float GYRO_IMMOB   = 20.0f;      // deg/s (se estiver em rad/s, ajuste!)
static const float AMAG_DEV     = 0.12f;      // variação aceitável do |a| em g (ex: 0.10~0.30)
static const uint32_t IMMOB_MS  = 800;       // tempo mínimo imóvel (1.0s)

// máquina de estados
enum FallState { IDLE, SUSPECT, WAIT_IMMOB };
static FallState fall_state = IDLE;

// buffer de votação (últimas M janelas)
static uint8_t win_ix = 0;
static uint8_t win_hits = 0;
static bool win_buf[ML_WIN_M] = {false, false};
static uint32_t suspect_start_ms = 0;
static float impact_peak_g = 0.0f;
static float impact_peak_dps = 0.0f; // GYRO FEATURE: armazena o maior pico do módulo do giroscópio observado durante o estado SUSPECT
static float evt_prob_queda_start = 0.0f;
static float evt_prob_normal_start = 1.0f;

// utilitário de magnitude
static inline float mag3(float x, float y, float z) {
  return sqrtf(x*x + y*y + z*z);
}

static bool equals_ignore_case(const char *a, const char *b) {
    if (a == nullptr || b == nullptr) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

// ============================================================================
// MONITORAMENTO: Memória, CPU e Timing
// ============================================================================
void printSystemStats(const char* context = "") {
    uint32_t free_heap = xPortGetFreeHeapSize();
    uint32_t min_heap = xPortGetMinimumEverFreeHeapSize();
    uint32_t window_ms = (EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE / 6) * EI_CLASSIFIER_INTERVAL_MS;
    float avg_inference_ms = (gInferenceCount > 0) ? ((float)gInferenceTotalAccumMs / (float)gInferenceCount) : 0.0f;
    float ml_duty = (window_ms > 0) ? (100.0f * avg_inference_ms / (float)window_ms) : 0.0f;
    
    Serial.printf("\n[MONITOR] %s\n", context);
    Serial.printf("  Heap livre agora: %u bytes | Heap mínimo histórico: %u bytes\n", 
                  free_heap, min_heap);

    Serial.printf("  Inference (ultima): total=%lums dsp=%lums class=%lums post=%lums\n",
                  gLastInferenceTotalMs,
                  gLastInferenceDspMs,
                  gLastInferenceClassMs,
                  gLastInferencePostMs);
    Serial.printf("  Inference (media): %.2fms em %lu execs | Carga ML estimada: %.1f%%\n",
                  avg_inference_ms,
                  gInferenceCount,
                  ml_duty);

    if (gTaskSensorHandle) {
        Serial.printf("  Stack livre Sensor Task: %u words\n", uxTaskGetStackHighWaterMark(gTaskSensorHandle));
    }
    if (gTaskMLHandle) {
        Serial.printf("  Stack livre ML Task: %u words\n", uxTaskGetStackHighWaterMark(gTaskMLHandle));
    }
    if (gTaskMQTTHandle) {
        Serial.printf("  Stack livre MQTT Task: %u words\n", uxTaskGetStackHighWaterMark(gTaskMQTTHandle));
    }
    if (gTaskPhysicsHandle) {
        Serial.printf("  Stack livre Physics Task: %u words\n", uxTaskGetStackHighWaterMark(gTaskPhysicsHandle));
    }
}

// estrutura com |a| e |g|
struct PhysSample {
  uint32_t t_ms;
  float a_g;    // |a| em g
  float g_dps;  // |g| em deg/s (ou unidade do driver)
};

// fila de streaming contínuo de física
static QueueHandle_t xQueuePhys = nullptr;

// ============================================================================
// EVENTOS MQTT (para evitar publish em múltiplas tasks)
// ============================================================================
enum MqttEventType : uint8_t { MQTT_EVT_TRUEFALL = 1 };

struct MqttEvent {
  uint8_t type;
    float prob_queda;
    float prob_normal;
  float peak_g;
    float peak_gyro;
  uint32_t t_ms;
};

static QueueHandle_t xQueueMqttEvents = nullptr;

// task que coleta continuamente |a| e |g|
void taskPhysics(void* pvParameters) {
  const TickType_t xFrequency = pdMS_TO_TICKS(EI_CLASSIFIER_INTERVAL_MS);
  TickType_t xLastWakeTime = xTaskGetTickCount();

  while (1) {
    float ax, ay, az, gx, gy, gz;
    M5.Imu.getAccel(&ax, &ay, &az);
    M5.Imu.getGyro (&gx, &gy, &gz);

    PhysSample s;
    s.t_ms  = millis();
    s.a_g   = mag3(ax, ay, az);   // assumindo accel em g
    s.g_dps = mag3(gx, gy, gz);   // geralmente deg/s

    // mantém fila "fresca" (se cheia, descarta o mais antigo)
    if (xQueuePhys) {
      if (xQueueSend(xQueuePhys, &s, 0) != pdTRUE) {
        PhysSample dump;
        xQueueReceive(xQueuePhys, &dump, 0);
        xQueueSend(xQueuePhys, &s, 0);
      }
    }

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void taskSensor(void* pvParameters) {

    const TickType_t xFrequency = pdMS_TO_TICKS(EI_CLASSIFIER_INTERVAL_MS);

    while (1) {
        xSemaphoreTake(xSemaphoreBufferFree, portMAX_DELAY);

        TickType_t xLastWakeTime = xTaskGetTickCount();

        while (feat_ix + 6 <= EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {

            float ax, ay, az, gx, gy, gz;
            M5.Imu.getAccel(&ax, &ay, &az);
            M5.Imu.getGyro (&gx, &gy, &gz);

            // --- Atualiza buffers circulares ---
            ax_buf[mm_idx] = ax;
            ay_buf[mm_idx] = ay;
            az_buf[mm_idx] = az;
            gx_buf[mm_idx] = gx;
            gy_buf[mm_idx] = gy;
            gz_buf[mm_idx] = gz;
            mm_idx = (mm_idx + 1) % N;

            // --- Calcula média móvel ---
            float ax_f = media_movel(ax_buf);
            float ay_f = media_movel(ay_buf);
            float az_f = media_movel(az_buf);
            float gx_f = media_movel(gx_buf);
            float gy_f = media_movel(gy_buf);
            float gz_f = media_movel(gz_buf);

            // --- Entra no buffer de features já filtrado ---
            features[feat_ix++] = ax_f;
            features[feat_ix++] = ay_f;
            features[feat_ix++] = az_f;
            features[feat_ix++] = gx_f;
            features[feat_ix++] = gy_f;
            features[feat_ix++] = gz_f;

            vTaskDelayUntil(&xLastWakeTime, xFrequency);
        }

        feat_ix = 0;
        xSemaphoreGive(xSemaphoreBufferFull);
    }
}

void taskML(void* pvParameters)
{
    static bool queda_ativa = false;

    // variáveis para checagem de imobilidade usando streaming Phys
    uint32_t immob_start = 0;
    float a_ref = 0.0f;
    bool a_ref_set = false;
    uint8_t immob_fail_count = 0;
    static const uint8_t IMMOB_FAIL_MAX = 2;

    while (1)
    {
        xSemaphoreTake(xSemaphoreBufferFull, portMAX_DELAY);

        signal_t signal;
        ei_impulse_result_t result;

        signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
        signal.get_data = &ei_get_feature_data;

        EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);

        if (res == EI_IMPULSE_OK)
        {
            // ===== LOGGING DE TIMING E MEMÓRIA =====
            int total_time = result.timing.dsp + result.timing.classification + 
                             result.timing.postprocessing + result.timing.anomaly;

            gLastInferenceTotalMs = total_time;
            gLastInferenceDspMs = result.timing.dsp;
            gLastInferenceClassMs = result.timing.classification;
            gLastInferencePostMs = result.timing.postprocessing;
            gInferenceCount++;
            gInferenceTotalAccumMs += total_time;

            Serial.printf("\n[INFERENCE] Tempo Total: %dms | DSP: %dms | Classification: %dms | Postproc: %dms\n",
                          total_time,
                          result.timing.dsp,
                          result.timing.classification,
                          result.timing.postprocessing);
            
            uint32_t heap_free = xPortGetFreeHeapSize();
            Serial.printf("[MEMORY] Heap livre: %u bytes\n", heap_free);
            
            // DEBUG — imprime todas as probabilidades
            // for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++)
            // {
            //     Serial.printf("%s: %.3f\n",
            //            result.classification[ix].label,
            //            result.classification[ix].value);
            // }
            // Serial.println("----");

            float prob_queda = -1.0f;
            float prob_normal = -1.0f;

            for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++)
            {
                const char *label = result.classification[ix].label;

                if (equals_ignore_case(label, "queda") ||
                    equals_ignore_case(label, "quedas") ||
                    equals_ignore_case(label, "fall") ||
                    equals_ignore_case(label, "falls")) {
                    prob_queda = result.classification[ix].value;
                }

                if (equals_ignore_case(label, "normal")) {
                    prob_normal = result.classification[ix].value;
                }
            }

            if (prob_queda < 0.0f && EI_CLASSIFIER_LABEL_COUNT >= 2) {
                // Fallback por índice esperado do modelo: [0]=Normal, [1]=Quedas.
                prob_normal = result.classification[0].value;
                prob_queda = result.classification[1].value;
            }

            if (prob_queda < 0.0f) prob_queda = 0.0f;
            
            // Log das probabilidades
            Serial.printf("[RESULT] Prob Queda: %.4f | Prob Normal: %.4f\n", 
                          prob_queda, prob_normal);

            // fallback se a classe "normal" nao existir no modelo
            if (prob_normal < 0.0f) {
                prob_normal = 1.0f - prob_queda;
                if (prob_normal < 0.0f) prob_normal = 0.0f;
                if (prob_normal > 1.0f) prob_normal = 1.0f;
            }

            // ===== etapa 1 — votação K de M janelas (persistência)
            bool hit = (prob_queda >= T_HIGH);
            if (win_buf[win_ix]) win_hits--;
            win_buf[win_ix] = hit;
            if (hit) win_hits++;
            win_ix = (win_ix + 1) % ML_WIN_M;

            // //Serial.printf("[KM] prob=%.3f hit=%d win_hits=%d/%d state=%d\n",
            //   prob_queda, hit ? 1 : 0, win_hits, ML_WIN_M, (int)fall_state);

            // IDLE -> SUSPECT quando K de M janelas batem
            if (fall_state == IDLE && win_hits >= ML_WIN_K) {
                fall_state = SUSPECT;
                suspect_start_ms = millis();
                impact_peak_g = 0.0f;
                impact_peak_dps = 0.0f; // GYRO FEATURE: zera o pico angular ao iniciar uma nova suspeita de queda
                evt_prob_queda_start = prob_queda;
                evt_prob_normal_start = prob_normal;
                immob_start = 0;
                a_ref_set = false;
                immob_fail_count = 0;
               // Serial.println("[STATE] SUSPECT (ML K/M)\n");
            }

            // ===== etapa 2 — impacto (pico de |a| e |g|)
            if (fall_state == SUSPECT) {
                PhysSample ps;
                uint8_t drained = 0;

                // GYRO FEATURE: além do pico de aceleração, também captura o maior pico angular durante a janela de suspeita
                while (xQueuePhys && xQueueReceive(xQueuePhys, &ps, 0) == pdTRUE && drained < 50) {
                    if (ps.a_g > impact_peak_g) impact_peak_g = ps.a_g;
                    if (ps.g_dps > impact_peak_dps) impact_peak_dps = ps.g_dps; // GYRO FEATURE: atualiza o pico do módulo do giroscópio
                    drained++;
                }

                // janela para achar impacto (~1.8s)
                if (millis() - suspect_start_ms <= 1800) {
                    // GYRO FEATURE: a transição para WAIT_IMMOB acontece se houver impacto linear suficiente OU rotação brusca suficiente
                    bool impact_ok =
                        (impact_peak_g >= IMPACT_G) ||
                        (impact_peak_g >= IMPACT_G_MID && impact_peak_dps >= IMPACT_GYRO);

                    if (impact_ok) {
                        fall_state = WAIT_IMMOB;
                        immob_start = 0;
                        a_ref_set = false;
                        immob_fail_count = 0;
                        //Serial.printf("[STATE] WAIT_IMMOB (a=%.2fg, gyro=%.2fdps)\n",
                                    //   impact_peak_g, impact_peak_dps);
                    }
                } else {
                    // passou a janela e não achou impacto relevante -> cancela
                    fall_state = IDLE;
                    //Serial.printf("[STATE] CANCEL (no impact, peak_a=%.2fg, peak_gyro=%.2fdps)\n",
                                //   impact_peak_g, impact_peak_dps);
                }
            }

            // ===== etapa 3 — imobilidade por IMMOB_MS
            if (fall_state == WAIT_IMMOB) {
                PhysSample ps;
                uint8_t drained = 0;

                while (xQueuePhys && xQueueReceive(xQueuePhys, &ps, 0) == pdTRUE && drained < 50) {

                    if (!a_ref_set) {
                        a_ref = ps.a_g;
                        a_ref_set = true;
                    }

                    bool gyro_ok = (ps.g_dps <= GYRO_IMMOB);
                    bool amag_ok = (fabsf(ps.a_g - a_ref) <= AMAG_DEV);

                    if (gyro_ok && amag_ok) {
                        immob_fail_count = 0;

                        if (immob_start == 0) {
                            immob_start = ps.t_ms;
                        }

                        if ((ps.t_ms - immob_start) >= IMMOB_MS) {
                            if (!queda_ativa) {
                                queda_ativa = true;
                                Serial.printf("\n✅ QUEDA CONFIRMADA! prob=%.3f peak_a=%.2fg peak_gyro=%.2fdps\n",
                                              prob_queda, impact_peak_g, impact_peak_dps);
                                // Log do estado da memória no momento da detecção
                                Serial.printf("[DETECTION_MEMORY] Heap livre: %u bytes\n", xPortGetFreeHeapSize());

                                if (xQueueMqttEvents) {
                                    MqttEvent ev;
                                    ev.type   = MQTT_EVT_TRUEFALL;
                                    ev.prob_queda  = evt_prob_queda_start;
                                    ev.prob_normal = evt_prob_normal_start;
                                    ev.peak_g = impact_peak_g;
                                    ev.peak_gyro = impact_peak_dps;
                                    ev.t_ms   = millis();
                                    xQueueSend(xQueueMqttEvents, &ev, 0);
                                }
                            }

                            fall_state = IDLE;
                            break;
                        }

                    } else {
                        immob_fail_count++;

                        if (immob_fail_count > IMMOB_FAIL_MAX) {
                            immob_start = 0;
                            a_ref = ps.a_g;
                            a_ref_set = true;
                            immob_fail_count = 0;
                        }
                    }

                    drained++;
                }
            }

            // histerese para liberar nova detecção
            if (prob_queda < 0.50f)
            {
                queda_ativa = false;
            }
        }
        else
        {
            Serial.printf("Erro na inferencia: %d\n", res);
        }

        xSemaphoreGive(xSemaphoreBufferFree);
    }
}

void reconnectMQTT() {
    while (!client.connected()) {
        //Serial.print("Conectando MQTT...");

        // DICA: se tiver mais de um ESP, use clientId único.
        bool ok = client.connect(
            "ESP32Client",
            nullptr,
            nullptr,
            willTopic,
            1,
            true,
            willMessage
        );

        if (ok) {
            //Serial.println("OK");
            client.publish(willTopic, "online", true);
        } else {
            //Serial.printf("Falhou rc=%d, tentando novamente...\n", client.state());
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }
}

void taskMQTT(void* pvParameters){
    uint32_t lastStatsReport = 0;

    while(1){

        if (WiFi.status() != WL_CONNECTED) {
            //Serial.println("Wi-Fi caiu! Reconectando...");
            WiFi.begin();
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if (!client.connected()) {
            reconnectMQTT();
        }

        client.loop();
        
        // A cada ~10 segundos, imprime estatísticas completas
        uint32_t now = millis();
        if (now - lastStatsReport > 10000) {
            lastStatsReport = now;
            Serial.println("\n========== RELATÓRIO PERIÓDICO ==========");
            printSystemStats("Status geral do sistema");
            Serial.println("========================================\n");
        }

        // >>> ALTERAÇÃO: taskMQTT é a ÚNICA que publica
        MqttEvent ev;
        if (xQueueMqttEvents && xQueueReceive(xQueueMqttEvents, &ev, pdMS_TO_TICKS(100))) {
            if(client.connected()){
                if (ev.type == MQTT_EVT_TRUEFALL) {
                    // publica o evento de queda confirmada
                    client.publish(fallTopic, "queda confirmada", false);
                    //Serial.printf("📡 MQTT enviado para %s (prob_queda=%.3f prob_normal=%.3f peak=%.2fg gyro=%.2f)\n",
                                //   fallTopic,
                                //   ev.prob_queda,
                                //   ev.prob_normal,
                                //   ev.peak_g,
                                //   ev.peak_gyro);

                    // opcional: também publicar JSON de evento em outro tópico
                    JsonDocument doc;
                    doc["event"]  = "true_fall";
                    doc["prob_queda"]  = ev.prob_queda;
                    doc["prob_normal"] = ev.prob_normal;
                    doc["peak_g"] = ev.peak_g;
                    doc["peak_gyro"] = ev.peak_gyro;
                    doc["t_ms"]   = ev.t_ms;

                    char buffer[256];
                    serializeJson(doc, buffer);
                    client.publish("ml/anomaly", buffer);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup() {
    Serial.begin(115200);

    WiFiManager wm;
    WiFi.mode(WIFI_STA);
    wm.setConfigPortalTimeout(200);
    wm.setConnectTimeout(15);

    bool ok = wm.autoConnect("ESP32-Setup");
    if (!ok) {
        //Serial.println("Falha ao conectar WiFi");
        ESP.restart();
    }
    //Serial.println("WiFi conectado!");
    //Serial.println(WiFi.localIP());

    client.setServer(mqtt_server, 1883);
    client.setKeepAlive(15);

    auto cfg = M5.config();
    M5.begin(cfg);

    if (!M5.Imu.begin()) {
        Serial.println("❌ Erro ao iniciar IMU!");
    } else {
        //Serial.println("IMU iniciado!");
    }

    // inicializa buffers média móvel com 0
    for (int i = 0; i < N; i++) {
        ax_buf[i] = ay_buf[i] = az_buf[i] = 0.0f;
        gx_buf[i] = gy_buf[i] = gz_buf[i] = 0.0f;
    }

    // fila para amostras físicas (impacto/imobilidade)
    xQueuePhys = xQueueCreate(200, sizeof(PhysSample));

    // fila para eventos MQTT (publicação centralizada na taskMQTT)
    xQueueMqttEvents = xQueueCreate(10, sizeof(MqttEvent));

    xSemaphoreBufferFull = xSemaphoreCreateBinary();
    xSemaphoreBufferFree = xSemaphoreCreateBinary();
    xSemaphoreGive(xSemaphoreBufferFree);

    xTaskCreatePinnedToCore(taskSensor,  "Sensor Task",  4096, NULL, 2, &gTaskSensorHandle, 1);
    xTaskCreatePinnedToCore(taskML,      "ML Task",      8192, NULL, 2, &gTaskMLHandle, 0);
    xTaskCreatePinnedToCore(taskMQTT,    "MQTT Task",    4096, NULL, 1, &gTaskMQTTHandle, 0);
    xTaskCreatePinnedToCore(taskPhysics, "Physics Task", 4096, NULL, 2, &gTaskPhysicsHandle, 1);
}

void loop() {
    vTaskDelete(NULL);
}