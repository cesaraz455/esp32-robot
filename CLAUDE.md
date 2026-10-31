# CLAUDE.md

Guía para Claude Code en este repositorio. Responde y comenta el código en español.

## Qué es

Firmware (Arduino + PlatformIO) del **ESP32-S3-DevKitC-1 N16R8** que va en la PCB del robot **auxi**
(proyecto universitario, robot de fútbol). El diseño de la PCB está en el repositorio hermano
`../auxi` (su `CLAUDE.md` es la fuente de verdad del hardware, Sección 6.1 = mapa de pines).
La cámara tiene su propio firmware en `../esp32cam-web`.

El código base viene de `../CodigoRobot.txt` y quedó copiado tal cual en `src/main.cpp`.

## Estructura

```
platformio.ini   - plataforma Bluepad32, placa esp32-s3-devkitc-1, libs
src/main.cpp     - todo el firmware (pines, motores, sensores, mando, UART)
```

## Hardware y pines (según main.cpp)

| Función | GPIO |
|---|---|
| I2C SDA / SCL | 7 / 15 |
| XSHUT frontal / izq / der | 5 / 6 / 4 |
| UART cámara RX / TX (`Serial1`) | 16 (sin conectar) / 17, 115200 8N1. GPIO18 sin usar (decisión del usuario) |
| LED frontal (rojo) / izq (amarillo) / der (verde) | 8 / 9 / 10 |
| TB6612FNG: PWMA, AIN1, AIN2 | 21, 40, 39 (motor izquierdo) |
| TB6612FNG: PWMB, BIN1, BIN2 | 1, 42, 2 (motor derecho) |
| TB6612FNG: STBY | 41 |

- Sensores VL53L0X: direcciones I2C asignadas al arranque con XSHUT: frontal `0x30`, izq `0x31`, der `0x32`. Bus a 100 kHz.
- Umbral de detección: 150 mm (enciende el LED correspondiente). `VELOCIDAD_BASE` = 200 (de 255).
- PWM por LEDC: 5 kHz, 8 bits, canales 0 (A) y 1 (B).
- Pines prohibidos en el S3 N16R8: 35, 36, 37 (PSRAM octal), 0/3/45/46 (arranque), 19/20 (USB), 43/44 (UART0). Libres: 11, 12, 13, 14, 16.

### Fuente de verdad

Este código es la fuente de verdad de los pines. SDA = GPIO7 y SCL = GPIO15 (`Wire.begin(7, 15)`).
`../auxi` (esquemático y PCB) todavía tiene SDA/SCL invertidos; está anotado como pendiente en la Sección 20 de su `CLAUDE.md`. No cambiar los pines aquí para igualar la PCB.

## Comportamiento

- **Mando Xbox por BLE** (Bluepad32; el S3 no tiene Bluetooth clásico). `axisY` (stick izq.) = velocidad, `axisRX` (stick der.) = giro, mezcla diferencial con zona muerta de 30.
- **UART hacia ESP32-CAM**: cada 800 ms (`INTERVALO_ENVIO_MS`) envía por `Serial1` (TX = GPIO17 → IO14 de la cámara; es la línea `UART_CAM_TO_ESP` de la PCB usada en sentido ESP → CAM por decisión del usuario; `UART_ESP_TO_CAM` (GPIO18 → IO13) no se usa) la línea `D,<frente>,<izq>,<der>\n` (mm; -1 = lectura inválida, `RangeStatus == 4`). La cámara la muestra en su página web. Cambiar este formato exige cambiar `esp32cam-web/src/main.cpp`.
- **UART desde ESP32-CAM**: solo se atiende si no hay mando conectado. Comandos terminados en `\n`, sin distinguir mayúsculas: `F`/`ADELANTE`, `B`/`ATRAS`, `L`/`IZQUIERDA`, `R`/`DERECHA`, `S`/`PARAR`.
- Los LED de proximidad no frenan los motores; solo indican obstáculo. (Reglas de la universidad: los LED deben ser visibles.)
- Al arrancar: autotest de LED (500 ms), motores parados, sensores inicializados uno a uno.

## Notas técnicas y trampas

- Se usa la API `ledcSetup`/`ledcAttachPin` del Arduino core **2.x**. Bluepad32 va sobre ese core; si se migra a core 3.x hay que cambiar a `ledcAttach`/`ledcWrite(pin, ...)`.
- `platformio.ini` usa `espressif32@6.10.0` y reemplaza el framework por el Arduino-ESP32 con Bluepad32 de `maxgerhardt/pio-framework-bluepad32` (`platform_packages`). Compila (verificado con PlatformIO 6.2). No se ha probado en hardware.
- `BP32.update()` y las lecturas `rangingTest` están en el mismo `loop()` con `delay(20)`; lecturas bloqueantes pueden hacer lenta la respuesta del mando.
- Si el mando se desconecta mientras se mueve, los motores conservan el último valor (no hay parada de seguridad). Candidato a mejora.
- El stick Y está invertido a propósito en `map(axisY, -512, 512, 255, -255)`.
- Secretos: este firmware no usa WiFi. Las credenciales WiFi viven solo en `../esp32cam-web/.env`.

## Comandos

```
pio run                    # compilar
pio run -t upload          # subir
pio device monitor         # serie a 115200
```
