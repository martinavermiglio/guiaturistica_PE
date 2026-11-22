#ifndef TELEMETRIA_H
#define TELEMETRIA_H

#include <Arduino.h>
#include <driver/i2s.h>

// ==================== GPS NEO-6M (UART) ====================
#define GPS_RX_PIN        4   // ESP32 RX <- NEO-6M TX
#define GPS_TX_PIN        5   // ESP32 TX -> NEO-6M RX (no imprescindible, solo lectura)
#define GPS_BAUDRATE      9600

// ==================== Micrófono INMP441 (I2S de entrada) ====================
#define PIN_I2S_IN_WS     15  // L/R Clock (word select)
#define PIN_I2S_IN_SCK    16  // Bit clock
#define PIN_I2S_IN_SD     17  // Serial data (micrófono -> ESP32)
#define I2S_IN_PORT       I2S_NUM_1   // Puerto distinto al usado por audio_ui.cpp (I2S_NUM_0)
#define I2S_SAMPLE_RATE   16000
#define DB_CALIBRACION_OFFSET  -22.0f // Offset empírico: AJUSTAR tras calibrar con un sonómetro de referencia

// ==================== Radar de movimiento (RCWL-0516 / HC-SR501) ====================
#define RADAR_PIN         6

// ==================== WiFi Sniffer (estimación de afluencia) ====================
#define SNIFFER_VENTANA_MS        60000  // Ventana deslizante (ms) para contar dispositivos "activos"
#define SNIFFER_MAX_DISPOSITIVOS  128     // Tamaño de la tabla de hashes de MAC vistos

// ==================== Cliente REST Sentilo ====================
#define SENTILO_HOST            "sentilo.example.org"
#define SENTILO_PORT            80
#define SENTILO_API_TOKEN       "TU_IDENTITY_KEY_AQUI"
#define SENTILO_PROVIDER_ID     "audioguia_gotic_01"
#define SENTILO_SENSOR_GPS        "gps"
#define SENTILO_SENSOR_RUIDO      "ruido_db"
#define SENTILO_SENSOR_MOVIMIENTO "movimiento"
#define SENTILO_SENSOR_AFLUENCIA  "afluencia"
#define SENTILO_INTERVALO_ENVIO_MS 30000 // Cada cuánto se reporta a Sentilo

// ==================== Zonas turísticas (Geofencing) ====================
struct ZonaTuristica {
    const char* nombre;
    double lat;
    double lon;
    double radioMetros;
    const char* audioURL;
};

// Inicialización de periféricos de Persona 2 (llamar desde setup / antes de la tarea)
void initGPS();
void initMicrofono();
void initRadar();
void initSnifferWiFi();

// Tarea principal para el Núcleo 0
void TaskSensors(void *pvParameters);

#endif
