#ifndef AUDIO_UI_H
#define AUDIO_UI_H

#include <Arduino.h>

// Pines Display OLED I2C
#define PIN_I2C_SDA      8
#define PIN_I2C_SCL      9
#define SCREEN_WIDTH     128
#define SCREEN_HEIGHT    64
#define OLED_RESET       -1
#define OLED_ADDR        0x3C

// Pines Amplificador MAX98357A I2S
#define PIN_I2S_OUT_LRC  10
#define PIN_I2S_OUT_BCLK 11
#define PIN_I2S_OUT_DIN  12

// Inicialización de periféricos de Persona 1
void initWiFi(const char* ssid, const char* password);
void initDisplay();
void initAudio();

// Tarea principal para el Núcleo 1
void TaskAudioDisplay(void *pvParameters);

#endif