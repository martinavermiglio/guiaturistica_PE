#include "audio_ui.h"
#include "shared_data.h"
#include <WiFi.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

#define I2S_OUT_PORT I2S_NUM_0

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

static unsigned long ultimoRefrescoOLED = 0;
const unsigned long INTERVALO_OLED_MS = 250;
static uint8_t pantallaActual = 0;
static unsigned long ultimoCambioMenu = 0;

static HTTPClient httpAudio;
static WiFiClient* streamAudio = nullptr;
static bool reproduciendoStream = false;

void initWiFi(const char* ssid, const char* password) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.println("Conectando WiFi...");
    display.println(ssid);
    display.display();

    unsigned long inicio = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - inicio < 10000) {
        delay(250);
    }

    wifi_conectado = (WiFi.status() == WL_CONNECTED);
}

void initDisplay() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println(F("[ERROR] Fallo al iniciar OLED GM009606V4.2"));
    }
    display.clearDisplay();
    display.display();
}

void initAudio() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_OUT_BCLK,
        .ws_io_num = PIN_I2S_OUT_LRC,
        .data_out_num = PIN_I2S_OUT_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_OUT_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_OUT_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_OUT_PORT);
}

// Emite un breve tono de aviso por el altavoz MAX98357A al entrar en una zona
static void reproducirTonoAviso(float freqHz, int duracionMs) {
    const int sampleRate = 16000;
    int totalMuestras = (sampleRate * duracionMs) / 1000;
    int16_t buffer[128];
    size_t escritos = 0;

    for (int i = 0; i < totalMuestras; i += 128) {
        int bloque = min(128, totalMuestras - i);
        for (int j = 0; j < bloque; j++) {
            float t = (float)(i + j) / sampleRate;
            buffer[j] = (int16_t)(sinf(2.0f * PI * freqHz * t) * 8000.0f);
        }
        i2s_write(I2S_OUT_PORT, buffer, bloque * sizeof(int16_t), &escritos, portMAX_DELAY);
    }
    i2s_zero_dma_buffer(I2S_OUT_PORT);
}

static void iniciarStreamWAV(const char* url) {
    if (reproduciendoStream) {
        httpAudio.end();
        reproduciendoStream = false;
    }

    // Tono inmediato de confirmación al detectar zona turística
    reproducirTonoAviso(880.0f, 200);

    // Si la URL sigue con el placeholder, no intenta conectar por HTTP
    if (strstr(url, "TU_SERVIDOR") != nullptr) {
        Serial.println(F("[AUDIO] Zona detectada (URL placeholder, solo tono de aviso)."));
        return;
    }

    httpAudio.begin(url);
    int code = httpAudio.GET();
    if (code == HTTP_CODE_OK) {
        streamAudio = httpAudio.getStreamPtr();
        // Saltar cabecera estándar WAV de 44 bytes
        uint8_t header[44];
        streamAudio->readBytes(header, 44);
        reproduciendoStream = true;
        Serial.printf("[AUDIO] Reproduciendo stream: %s\n", url);
    } else {
        Serial.printf("[AUDIO] Error HTTP %d al abrir %s\n", code, url);
        httpAudio.end();
    }
}

static void procesarStreamAudio() {
    if (!reproduciendoStream || streamAudio == nullptr) return;

    if (httpAudio.connected() && streamAudio->available() > 0) {
        uint8_t buffer[512];
        int leidos = streamAudio->readBytes(buffer, min((int)sizeof(buffer), streamAudio->available()));
        if (leidos > 0) {
            size_t escritos = 0;
            i2s_write(I2S_OUT_PORT, buffer, leidos, &escritos, pdMS_TO_TICKS(50));
        }
    } else if (!httpAudio.connected()) {
        httpAudio.end();
        reproduciendoStream = false;
        i2s_zero_dma_buffer(I2S_OUT_PORT);
    }
}

void actualizarPantalla() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);

    display.print("BARRI GOTIC ");
    display.println(wifi_conectado ? "[WiFi]" : "[OFF]");
    display.println("---------------------");

    char zonaLocal[32];
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        strncpy(zonaLocal, zona_actual, sizeof(zonaLocal));
        xSemaphoreGive(dataMutex);
    } else {
        strcpy(zonaLocal, "Cargando...");
    }

    if (millis() - ultimoCambioMenu > 4000) {
        pantallaActual = (pantallaActual + 1) % 2;
        ultimoCambioMenu = millis();
    }

    if (pantallaActual == 0) {
        display.printf("Zona: %s\n", zonaLocal);
        display.printf("Audio: %s\n", reproduciendoStream ? "REPRODUCIENDO" : "EN ESPERA");
        display.printf("GPS: %.4f, %.4f\n", latitud_actual, longitud_actual);
    } else {
        display.printf("Ruido: %.1f dB\n", nivel_ruido_db);
        display.printf("Radar: %s\n", movimiento_detectado ? "MOVIMIENTO" : "DESPEJADO");
        display.printf("Afluencia: %d disp.\n", dispositivos_cercanos);
    }

    display.display();
}

void TaskAudioDisplay(void *pvParameters) {
    // Tono corto al arrancar para verificar que el MAX98357A y el altavoz funcionan
    reproducirTonoAviso(523.25f, 150);

    for (;;) {
        if (nueva_pista_disponible) {
            char urlLocal[128];
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                strncpy(urlLocal, url_audio_actual, sizeof(urlLocal));
                nueva_pista_disponible = false;
                xSemaphoreGive(dataMutex);

                iniciarStreamWAV(urlLocal);
            }
        }

        procesarStreamAudio();
        audio_reproduciendo = reproduciendoStream;

        if (millis() - ultimoRefrescoOLED >= INTERVALO_OLED_MS) {
            actualizarPantalla();
            ultimoRefrescoOLED = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}