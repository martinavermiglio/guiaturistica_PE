#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Mutex para proteger strings compartidos entre Núcleo 0 y Núcleo 1
extern SemaphoreHandle_t dataMutex;

// Variables escritas por Persona 2 (Núcleo 0) y leídas por Persona 1 (Núcleo 1)
extern volatile float latitud_actual;
extern volatile float longitud_actual;
extern volatile float nivel_ruido_db;
extern volatile bool movimiento_detectado;
extern volatile int dispositivos_cercanos;

// Datos de Geofencing / Pista asignada por Persona 2
extern char zona_actual[32];
extern char url_audio_actual[128];
extern volatile bool nueva_pista_disponible;

// Estado reportado por Persona 1 (Núcleo 1)
extern volatile bool wifi_conectado;
extern volatile bool audio_reproduciendo;

#endif