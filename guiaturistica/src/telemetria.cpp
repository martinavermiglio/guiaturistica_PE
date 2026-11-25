// Librerías necesarias (instalar vía Library Manager / lib_deps de PlatformIO):
//   - TinyGPSPlus      (mikalhart/TinyGPSPlus)
//   - ArduinoJson       (bblanchon/ArduinoJson, v6)
//   - Core ESP32 Arduino (WiFi, HTTPClient, esp_wifi, driver/i2s ya incluidos en el framework)

#include "telemetria.h"
#include "shared_data.h"
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Objetos y estado interno
// ---------------------------------------------------------------------------
static TinyGPSPlus gps;
static HardwareSerial SerialGPS(2); // UART2

static const ZonaTuristica ZONAS[] = {
    {"Plaza Sant Jaume",      41.38295, 2.17743, 40.0, "http://TU_SERVIDOR/audios/sant_jaume.mp3"},
    {"Catedral de Barcelona", 41.38397, 2.17587, 50.0, "http://TU_SERVIDOR/audios/catedral.mp3"},
    {"Placa Reial",           41.37975, 2.17529, 35.0, "http://TU_SERVIDOR/audios/placa_reial.mp3"},
    {"Call Jueu",             41.38271, 2.17618, 30.0, "http://TU_SERVIDOR/audios/call.mp3"},
};
static const uint8_t NUM_ZONAS = sizeof(ZONAS) / sizeof(ZONAS[0]);
static int8_t zonaAnteriorIdx = -1;

static int32_t bufferI2S[256];

struct RegistroMAC {
    uint32_t hashMac;
    unsigned long timestampMs;
    bool enUso;
};
static RegistroMAC tablaMACs[SNIFFER_MAX_DISPOSITIVOS];

static unsigned long ultimoEnvioSentilo = 0;

// ---------------------------------------------------------------------------
// GPS + Geofencing
// ---------------------------------------------------------------------------
static double distanciaHaversineMetros(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371000.0; // Radio terrestre en metros
    double dLat = radians(lat2 - lat1);
    double dLon = radians(lon2 - lon1);
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(radians(lat1)) * cos(radians(lat2)) *
               sin(dLon / 2) * sin(dLon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return R * c;
}

static void procesarGPS() {
    while (SerialGPS.available() > 0) {
        gps.encode(SerialGPS.read());
    }

    if (!gps.location.isValid() || !gps.location.isUpdated()) {
        return;
    }

    double lat = gps.location.lat();
    double lon = gps.location.lng();

    // Publicar coordenadas para que Persona 1 las muestre en el OLED
    latitud_actual = lat;
    longitud_actual = lon;

    // Comprobar si estamos dentro de alguna zona turística registrada
    for (uint8_t i = 0; i < NUM_ZONAS; i++) {
        double dist = distanciaHaversineMetros(lat, lon, ZONAS[i].lat, ZONAS[i].lon);
        if (dist <= ZONAS[i].radioMetros) {
            if (zonaAnteriorIdx != (int8_t)i) {
                if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    strncpy(zona_actual, ZONAS[i].nombre, sizeof(zona_actual) - 1);
                    zona_actual[sizeof(zona_actual) - 1] = '\0';
                    strncpy(url_audio_actual, ZONAS[i].audioURL, sizeof(url_audio_actual) - 1);
                    url_audio_actual[sizeof(url_audio_actual) - 1] = '\0';
                    nueva_pista_disponible = true;
                    xSemaphoreGive(dataMutex);
                }
                zonaAnteriorIdx = i;
                Serial.printf("[GEOFENCING] Entrada en zona: %s\n", ZONAS[i].nombre);
            }
            return; // Zona activa encontrada, no comprobar el resto
        }
    }
    // Si no estamos dentro de ninguna zona registrada, se mantiene la última pista activa
}

// ---------------------------------------------------------------------------
// Micrófono INMP441 -> dB (RMS)
// ---------------------------------------------------------------------------
static void procesarMicrofono() {
    size_t bytesLeidos = 0;
    esp_err_t res = i2s_read(I2S_IN_PORT, bufferI2S, sizeof(bufferI2S), &bytesLeidos, pdMS_TO_TICKS(50));
    if (res != ESP_OK || bytesLeidos == 0) return;

    int muestras = bytesLeidos / sizeof(int32_t);
    if (muestras <= 0) return;

    double media = 0;
    static double valores[256];

    for (int i = 0; i < muestras; i++) {
        // El INMP441 entrega 24 bits útiles alineados a la izquierda en la palabra de 32 bits
        int32_t muestra24 = bufferI2S[i] >> 8;
        valores[i] = (double)muestra24;
        media += valores[i];
    }
    media /= muestras;

    // Quitar el offset DC antes de calcular el RMS (mejora la precisión)
    double suma = 0;
    for (int i = 0; i < muestras; i++) {
        double centrado = valores[i] - media;
        suma += centrado * centrado;
    }

    double rms = sqrt(suma / muestras);
    if (rms < 1.0) rms = 1.0; // Evitar log(0)

    double db = 20.0 * log10(rms) + DB_CALIBRACION_OFFSET;
    if (db < 0) db = 0;

    nivel_ruido_db = (float)db;
}

// ---------------------------------------------------------------------------
// Radar de movimiento
// ---------------------------------------------------------------------------
static void procesarRadar() {
    movimiento_detectado = (digitalRead(RADAR_PIN) == HIGH);
}

// ---------------------------------------------------------------------------
// Sniffer WiFi (estimación de afluencia por dispositivos únicos)
// ---------------------------------------------------------------------------
static uint32_t hashMacAddress(const uint8_t* mac) {
    // Hash FNV-1a, no reversible: nunca se guarda ni se envía la MAC real
    // (privacidad por diseño, recomendable para cumplir con el RGPD).
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) {
        h ^= mac[i];
        h *= 16777619u;
    }
    return h;
}

static void registrarDispositivo(uint32_t hash) {
    unsigned long ahora = millis();

    for (int i = 0; i < SNIFFER_MAX_DISPOSITIVOS; i++) {
        if (tablaMACs[i].enUso && tablaMACs[i].hashMac == hash) {
            tablaMACs[i].timestampMs = ahora;
            return;
        }
    }
    for (int i = 0; i < SNIFFER_MAX_DISPOSITIVOS; i++) {
        if (!tablaMACs[i].enUso) {
            tablaMACs[i].hashMac = hash;
            tablaMACs[i].timestampMs = ahora;
            tablaMACs[i].enUso = true;
            return;
        }
    }
    // Tabla llena (caso extremo de mucha afluencia): se ignora el nuevo dispositivo
}

// Cabecera mínima de una trama de gestión 802.11
typedef struct {
    uint16_t frame_ctrl;
    uint16_t duration_id;
    uint8_t addr1[6];
    uint8_t addr2[6]; // MAC de origen (dispositivo emisor)
    uint8_t addr3[6];
    uint16_t seq_ctrl;
} wifi_mgmt_hdr_t;

static void IRAM_ATTR callbackPromiscuo(void* buf, wifi_promiscuous_pkt_type_t tipo) {
    if (tipo != WIFI_PKT_MGMT) return;

    const wifi_promiscuous_pkt_t* paquete = (wifi_promiscuous_pkt_t*)buf;
    const uint8_t* payload = paquete->payload;

    uint16_t frameControl = payload[0] | (payload[1] << 8);
    uint8_t subtipo = (frameControl >> 4) & 0x0F;

    if (subtipo != 0x04) return; // Solo Probe Request

    const wifi_mgmt_hdr_t* cabecera = (const wifi_mgmt_hdr_t*)payload;
    uint32_t hash = hashMacAddress(cabecera->addr2);
    registrarDispositivo(hash);
}

static void expirarYContarDispositivos() {
    unsigned long ahora = millis();
    int contador = 0;
    for (int i = 0; i < SNIFFER_MAX_DISPOSITIVOS; i++) {
        if (tablaMACs[i].enUso) {
            if (ahora - tablaMACs[i].timestampMs > SNIFFER_VENTANA_MS) {
                tablaMACs[i].enUso = false;
            } else {
                contador++;
            }
        }
    }
    dispositivos_cercanos = contador;
}

// ---------------------------------------------------------------------------
// Cliente REST Sentilo
// ---------------------------------------------------------------------------
static bool enviarObservacionSentilo(const char* sensorId, const String& valor) {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    String url = String("http://") + SENTILO_HOST + ":" + SENTILO_PORT +
                  "/data/" + SENTILO_PROVIDER_ID + "/" + sensorId;

    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("IDENTITY_KEY", SENTILO_API_TOKEN);

    StaticJsonDocument<256> doc;
    JsonArray sensores = doc.createNestedArray("sensors");
    JsonObject sensor = sensores.createNestedObject();
    sensor["sensor"] = sensorId;
    JsonArray observaciones = sensor.createNestedArray("observations");
    JsonObject obs = observaciones.createNestedObject();
    obs["value"] = valor;

    String cuerpo;
    serializeJson(doc, cuerpo);

    int codigoRespuesta = http.POST(cuerpo);
    bool ok = (codigoRespuesta == 200 || codigoRespuesta == 201);

    if (!ok) {
        Serial.printf("[SENTILO] Error al enviar %s: HTTP %d\n", sensorId, codigoRespuesta);
    }

    http.end();
    return ok;
}

static void enviarTelemetriaCompleta() {
    float lat, lon, ruido;
    bool movimiento;
    int dispositivos;

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        lat = latitud_actual;
        lon = longitud_actual;
        ruido = nivel_ruido_db;
        movimiento = movimiento_detectado;
        dispositivos = dispositivos_cercanos;
        xSemaphoreGive(dataMutex);
    } else {
        return; // No se pudo leer el estado compartido; se reintenta en el siguiente ciclo
    }

    char bufferGPS[32];
    char bufferDB[16];
    snprintf(bufferGPS, sizeof(bufferGPS), "%.5f,%.5f", lat, lon);
    snprintf(bufferDB, sizeof(bufferDB), "%.1f", ruido);

    enviarObservacionSentilo(SENTILO_SENSOR_GPS, String(bufferGPS));
    enviarObservacionSentilo(SENTILO_SENSOR_RUIDO, String(bufferDB));
    enviarObservacionSentilo(SENTILO_SENSOR_MOVIMIENTO, movimiento ? "1" : "0");
    enviarObservacionSentilo(SENTILO_SENSOR_AFLUENCIA, String(dispositivos));
}

// ---------------------------------------------------------------------------
// Inicialización de periféricos
// ---------------------------------------------------------------------------
void initGPS() {
    SerialGPS.begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
}

void initMicrofono() {
    i2s_config_t config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = I2S_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false
    };

    i2s_pin_config_t pines = {
        .bck_io_num = PIN_I2S_IN_SCK,
        .ws_io_num = PIN_I2S_IN_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_I2S_IN_SD
    };

    i2s_driver_install(I2S_IN_PORT, &config, 0, NULL);
    i2s_set_pin(I2S_IN_PORT, &pines);
}

void initRadar() {
    pinMode(RADAR_PIN, INPUT);
}

void initSnifferWiFi() {
    for (int i = 0; i < SNIFFER_MAX_DISPOSITIVOS; i++) {
        tablaMACs[i].enUso = false;
    }

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&callbackPromiscuo);

    // Se fija el canal al de la red WiFi ya conectada (sin hopping) para no
    // perder la conexión con el AP, que Persona 1 necesita para el streaming
    // de audio. Limitación conocida: solo se detectan dispositivos que estén
    // en ese mismo canal.
    uint8_t canalActual;
    wifi_second_chan_t canalSecundario;
    esp_wifi_get_channel(&canalActual, &canalSecundario);
    esp_wifi_set_channel(canalActual, WIFI_SECOND_CHAN_NONE);
}

// ---------------------------------------------------------------------------
// Tarea principal (Núcleo 0)
// ---------------------------------------------------------------------------
void TaskSensors(void *pvParameters) {
    initGPS();
    initMicrofono();
    initRadar();
    initSnifferWiFi();

    for (;;) {
        procesarGPS();
        procesarMicrofono();
        procesarRadar();
        expirarYContarDispositivos();

        if (millis() - ultimoEnvioSentilo >= SENTILO_INTERVALO_ENVIO_MS) {
            enviarTelemetriaCompleta();
            ultimoEnvioSentilo = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}