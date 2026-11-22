#include <Arduino.h>
#include "shared_data.h"
#include "audio_ui.h"
#include "telemetria.h" // <-- Descomentado para integrar Persona 2

// Credenciales Wi-Fi
const char* WIFI_SSID = "TU_SSID";
const char* WIFI_PASS = "TU_PASSWORD";

// Definición real de las variables compartidas (declaradas como extern en shared_data.h)
SemaphoreHandle_t dataMutex = NULL;

volatile float latitud_actual = 41.3825;
volatile float longitud_actual = 2.1769;
volatile float nivel_ruido_db = 0.0;
volatile bool movimiento_detectado = false;
volatile int dispositivos_cercanos = 0;

char zona_actual[32] = "Plaza Sant Jaume";
char url_audio_actual[128] = "";
volatile bool nueva_pista_disponible = false;

volatile bool wifi_conectado = false;
volatile bool audio_reproduciendo = false;

TaskHandle_t TaskAudioDisplayHandle = NULL;
TaskHandle_t TaskSensorsHandle = NULL;

void setup() {
    Serial.begin(115200);

    // Crear el Mutex antes de lanzar las tareas
    dataMutex = xSemaphoreCreateMutex();

    // Inicialización Persona 1 (Núcleo 1)
    initDisplay();
    initWiFi(WIFI_SSID, WIFI_PASS);
    initAudio();

    // Lanzar tarea de Persona 1 en Núcleo 1 (Pila amplia de 8192 bytes para decodificador MP3)
    xTaskCreatePinnedToCore(
        TaskAudioDisplay,
        "TaskAudioDisplay",
        8192,
        NULL,
        2,
        &TaskAudioDisplayHandle,
        1
    );

    // Lanzar tarea de Persona 2 en Núcleo 0 (initGPS, initMicrofono, etc. se ejecutan dentro)
    xTaskCreatePinnedToCore(
        TaskSensors,
        "TaskSensors",
        8192,
        NULL,
        1,
        &TaskSensorsHandle,
        0
    );
}

void loop() {
    vTaskDelete(NULL);
}
