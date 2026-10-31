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
| UART cámara RX / TX (`Serial1`) | 17 / 18, 115200 8N1 |
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
- **UART desde ESP32-CAM**: solo se atiende si no hay mando conectado. Comandos terminados en `\n`, sin distinguir mayúsculas: `F`/`ADELANTE`, `B`/`ATRAS`, `L`/`IZQUIERDA`, `R`/`DERECHA`, `S`/`PARAR`.
- Los LED de proximidad no frenan los motores; solo indican obstáculo. (Reglas de la universidad: los LED deben ser visibles.)
- Al arrancar: autotest de LED (500 ms), motores parados, sensores inicializados uno a uno.

## Notas técnicas y trampas

- Se usa la API `ledcSetup`/`ledcAttachPin` del Arduino core **2.x**. Bluepad32 va sobre ese core; si se migra a core 3.x hay que cambiar a `ledcAttach`/`ledcWrite(pin, ...)`.
- `platformio.ini` usa la plataforma fork de Bluepad32; **la URL no está verificada**, revisar la documentación oficial si falla la resolución.
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
