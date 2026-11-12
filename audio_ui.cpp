#include "audio_ui.h"
#include "shared_data.h"
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Audio.h"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Audio audio;

static unsigned long ultimoRefrescoOLED = 0;
const unsigned long INTERVALO_OLED_MS = 250; // 4 FPS fluidos sin bloquear audio
static uint8_t pantallaActual = 0;
static unsigned long ultimoCambioMenu = 0;

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
    // Configura pines I2S (BCLK, LRC, DOUT) usando el periférico I2S0 por defecto
    audio.setPinout(PIN_I2S_OUT_BCLK, PIN_I2S_OUT_LRC, PIN_I2S_OUT_DIN);
    audio.setVolume(15); // Rango: 0 a 21
}

void actualizarPantalla() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);

    // Cabecera común
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

    // Alterna vistas cada 4 segundos para mostrar toda la telemetría limpia
    if (millis() - ultimoCambioMenu > 4000) {
        pantallaActual = (pantallaActual + 1) % 2;
        ultimoCambioMenu = millis();
    }

    if (pantallaActual == 0) {
        // Vista 1: Estado de Audioguía y Zona
        display.printf("Zona: %s\n", zonaLocal);
        display.printf("Audio: %s\n", audio.isRunning() ? "REPRODUCIENDO" : "EN ESPERA");
        display.printf("GPS: %.4f, %.4f\n", latitud_actual, longitud_actual);
    } else {
        // Vista 2: Sensores de Afluencia
        display.printf("Ruido: %.1f dB\n", nivel_ruido_db);
        display.printf("Radar: %s\n", movimiento_detectado ? "MOVIMIENTO" : "DESPEJADO");
        display.printf("Afluencia: %d disp.\n", dispositivos_cercanos);
    }

    display.display();
}

void TaskAudioDisplay(void *pvParameters) {
    // URL de prueba inicial (puedes cambiarla por un MP3 real o servidor local)
    // Ejemplo: audio.connecttohost("http://radioparadise.com");

    for (;;) {
        // 1. Alimentar el decodificador de audio I2S (MÁXIMA PRIORIDAD)
        audio.loop();
        audio_reproduciendo = audio.isRunning();

        // 2. Comprobar si Persona 2 (Geofencing) ha activado una nueva pista turística
        if (nueva_pista_disponible && wifi_conectado) {
            char urlLocal[128];
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                strncpy(urlLocal, url_audio_actual, sizeof(urlLocal));
                nueva_pista_disponible = false;
                xSemaphoreGive(dataMutex);

                audio.stopSong();
                audio.connecttohost(urlLocal);
            }
        }

        // 3. Refresco no bloqueante de la pantalla OLED
        if (millis() - ultimoRefrescoOLED >= INTERVALO_OLED_MS) {
            actualizarPantalla();
            ultimoRefrescoOLED = millis();
        }

        // Cede 1 tick al scheduler para evitar watchdog sin cortar el audio
        vTaskDelay(1);
    }
}

// Callbacks opcionales de ESP32-audioI2S para depuración por Serial
void audio_info(const char *info) {
    Serial.print("Audio Info: ");
    Serial.println(info);
}
