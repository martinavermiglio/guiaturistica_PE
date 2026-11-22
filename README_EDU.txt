## Nuevas implementaciones aplicadas

Resumen de pines usados (ajustables): GPS por UART2 (RX=4), micrófono INMP441 por I2S en `I2S_NUM_1` (WS=15, SCK=16, SD=17, puerto distinto al de salida de audio), radar en el pin 6.

- **GPS + Geofencing**: parseo NMEA con `TinyGPS++`; una tabla `ZonaTuristica[]` (nombre, lat/lon, radio, URL de audio) se compara contra la posición actual con la fórmula de **Haversine**. Al entrar en una zona nueva, actualiza `zona_actual` / `url_audio_actual` bajo mutex y activa `nueva_pista_disponible`.
- **INMP441 → dB**: lectura I2S de muestras de 32 bits, extracción de los 24 bits útiles, eliminación del offset DC y cálculo de **RMS**, convertido a dB con `20·log10(rms) + offset`. El offset (`DB_CALIBRACION_OFFSET`) es un valor de partida: **hay que calibrarlo con un sonómetro de referencia**.
- **Radar**: lectura digital simple, sin lógica adicional.
- **Estimación de afluencia (sniffer WiFi)**: modo promiscuo capturando únicamente *Probe Requests* (gestión 802.11, subtipo 4). Por privacidad (y para no almacenar MACs reales, relevante bajo RGPD al tratarse de datos de vía pública), cada MAC se reduce a un **hash FNV-1a no reversible** antes de guardarse en una tabla con ventana deslizante de 60 s; el conteo de entradas vivas es `dispositivos_cercanos`.
  - **Limitación conocida**: el canal WiFi se fija al del punto de acceso (sin *channel hopping*) para no romper la conexión que necesita el audio en streaming. Esto significa que solo se detectan dispositivos que estén buscando redes en ese mismo canal — es una simplificación razonable para el alcance del proyecto, no un conteo exhaustivo.
- **Cliente REST Sentilo**: `enviarObservacionSentilo()` construye el JSON (`sensors[].observations[].value`) con `ArduinoJson` y hace `POST` a `http://HOST:PORT/data/{provider}/{sensor}` con cabecera `IDENTITY_KEY`. Cada 30 s (configurable) se envían los 4 sensores: GPS, ruido, movimiento y afluencia.


## Instrucciones

**A. Añadir en platformio.ini:
```ini
lib_deps =
    mikalhart/TinyGPSPlus
    bblanchon/ArduinoJson
```



**B. Testear cada sensor por separado antes de integrar** (evita depurar cinco cosas a la vez):
1. **GPS**: comenta temporalmente el resto y haz un sketch mínimo que solo imprima `gps.location.lat()/lng()` por Serial; sal al exterior (el NEO-6M no fija posición en interiores) y comprueba que `isValid()` se vuelve `true` en 30–60 s.
2. **INMP441**: imprime el valor en dB por Serial Plotter; habla cerca del micro y comprueba que sube; ajusta `DB_CALIBRACION_OFFSET` comparando contra un sonómetro o app de referencia.
3. **Radar**: `digitalRead` simple con un LED o print; verifica que cambia al mover la mano delante.
4. **Sniffer WiFi**: imprime `dispositivos_cercanos` por Serial; prueba con 1, 2 y 3 móviles con WiFi activado cerca y comprueba que el número sube y baja al expirar la ventana de 60 s.
5. **Sentilo**: antes de probar desde el ESP32, valida el endpoint con `curl` para descartar errores de token o de formato:
   ```bash
   curl -X POST "http://SENTILO_HOST:PORT/data/audioguia_gotic_01/ruido_db" \
     -H "Content-Type: application/json" -H "IDENTITY_KEY: TU_TOKEN" \
     -d '{"sensors":[{"sensor":"ruido_db","observations":[{"value":"42.3"}]}]}'
   ```

**C. Integración con el resto del proyecto**
En `main.cpp`, descomenta:
```cpp
#include "telemetria.h"
// ...
initSensors(); // o llama a initGPS()/initMicrofono()/initRadar()/initSnifferWiFi() si prefieres no agrupar
// ...
xTaskCreatePinnedToCore(TaskSensors, "TaskSensors", 8192, NULL, 1, &TaskSensorsHandle, 0);
```
Sube la pila a 8192 (ya está así) porque `HTTPClient` + `ArduinoJson` consumen más RAM que el resto de tareas; si ves resets por *stack overflow* o *watchdog*, es el primer sitio donde mirar.

**D. Testeo conjunto**
- Verifica con el monitor serie que no hay *panics* ni resets al cabo de varios minutos.
- Comprueba en el OLED que la vista de "Afluencia/Ruido/Radar" ya muestra datos reales (no ceros).
- Camina por una zona geocercada real y confirma que el audio cambia de pista automáticamente.
- Revisa en el panel de Sentilo que llegan las 4 observaciones con la periodicidad esperada.

