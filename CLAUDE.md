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

## Flujo de ejecución

Todo está en `src/main.cpp`: `setup()` corre una vez al encender y `loop()` se repite (~20 ms por vuelta).

### `setup()` (una sola vez)

1. Abre `Serial` (depuración) y `Serial1` (UART hacia la ESP32-CAM, 115200).
2. Autotest de LED: los 3 se encienden 500 ms.
3. Inicia motores (PWM + TB6612) y los deja **parados**.
4. Apaga los 3 sensores (XSHUT en LOW) y abre el bus I2C.
5. Enciende los sensores **uno a uno** (los tres nacen con la misma dirección I2C; así cada uno recibe la suya: frontal `0x30`, izq. `0x31`, der. `0x32`). Si responde, queda midiendo solo en modo continuo y se marca `ok`; si no, imprime `[ERROR]` y se marca no-ok, pero el programa sigue.
6. Inicia Bluetooth (Bluepad32).

### `loop()` (cada vuelta)

1. **Sensores (sin bloqueo):** por cada sensor `ok`, si hay una medición nueva se guarda en mm (`-1` si está fuera de rango); si no, se conserva la última. Un sensor no-ok no se toca (distancia siempre `-1`, cero tráfico I2C).
2. **LED de proximidad:** distancia entre 1 y 150 mm enciende el LED de ese lado; `-1` lo apaga.
3. **Mando Xbox:** `BP32.update()`; si hay mando conectado, sus sticks mueven los motores (`controlDiferencial`).
4. **Comandos UART de la cámara:** solo si no hay mando conectado.
5. **Envío a la cámara:** cada 800 ms, `D,frente,izq,der\n`.
6. `delay(20)` y vuelta al paso 1.

### `controlDiferencial(axisY, axisRX)`

1. `v` = stick izq. vertical → -255…255 (invertido a propósito). Si `|v| < 30` → 0.
2. `w` = stick der. horizontal → -255…255. Zona muerta de 50 con reescalado, y luego × `GIRO_PORCENTAJE` (50 %).
3. `izq = v + w`, `der = v - w`. Si alguno pasa de 255, se escalan los dos por igual.
4. Se aplica `CORRECCION_IZQ/DER` y se envía a cada motor.

| Sticks | Resultado |
|---|---|
| Izq. adelante, der. al centro | Ambas ruedas adelante |
| Izq. al centro, der. a la derecha | Giro en el sitio (izq. avanza, der. retrocede) |
| Izq. adelante + der. a un lado | Avanza en curva |
| Ambos al centro | Motores parados |

### Sin sensores conectados

Los sensores **no frenan ni limitan los motores**: solo LED y datos a la cámara. Sin ellos el robot se maneja igual: salen tres `[ERROR]` al arrancar, el `loop()` sigue en ~20 ms, los LED quedan apagados y la cámara recibe `D,-1,-1,-1`. El arranque sí tarda ~2.7 s por los intentos de `begin()`.

## Depuración por serie

En el entorno normal `Serial` sale por UART0 (GPIO43/44), no por el USB. Para verlo por el cable USB hay un entorno aparte, `debug` (activa `ARDUINO_USB_MODE=1` y `ARDUINO_USB_CDC_ON_BOOT=1`):

```
pio run -e debug -t upload
pio device monitor -e debug
```

Con `DEBUG_SERIAL = 1` en `main.cpp` se imprime cada 250 ms:

```
[MANDO] axisY=.. axisRX=.. | v=.. w=.. | MotorIzq=.. MotorDer=..
[SENS]  F: ok=.. mm=.. | I: ok=.. mm=.. | D: ok=.. mm=.. | LEDs ... | loop=..ms mando=..
```

- `[MANDO]` sirve para validar el mapeo de los sticks **sin batería**: nada se mueve, pero se ve lo que recibiría cada motor.
- `[SENS]` muestra qué sensores respondieron, su distancia y la duración del `loop()` (`loop=20ms` es lo normal).
- Los errores `Wire.cpp ... i2cRead returned Error -1` aparecen solo al arrancar, durante `begin()` de sensores ausentes.

## Notas técnicas y trampas

- Se usa la API `ledcSetup`/`ledcAttachPin` del Arduino core **2.x**. Bluepad32 va sobre ese core; si se migra a core 3.x hay que cambiar a `ledcAttach`/`ledcWrite(pin, ...)`.
- `platformio.ini` usa `espressif32@6.10.0` y reemplaza el framework por el Arduino-ESP32 con Bluepad32 de `maxgerhardt/pio-framework-bluepad32` (`platform_packages`). Compila (verificado con PlatformIO 6.2). No se ha probado en hardware.
- Los VL53L0X trabajan en **modo continuo** (`startRangeContinuous(50)`): el `loop()` solo pregunta `isRangeComplete()` y guarda el último valor, sin esperar. Un sensor que no inicializa (`okFrontal/okIzq/okDer = false`) no se vuelve a consultar. Antes se usaba `rangingTest` (bloqueante): sin sensores cada vuelta tardaba ~3.7 s y el mando quedaba inutilizable, y las mediciones quedaban sin inicializar.
- Si el mando se desconecta mientras se mueve, los motores conservan el último valor (no hay parada de seguridad). Candidato a mejora.
- El stick Y está invertido a propósito en `map(axisY, -512, 512, 255, -255)`.
- Secretos: este firmware no usa WiFi. Las credenciales WiFi viven solo en `../esp32cam-web/.env`.

## Comandos

```
pio run                    # compilar (firmware normal)
pio run -t upload          # subir firmware normal (uso del robot)
pio run -e debug -t upload # subir firmware de depuración (prints por USB)
pio device monitor -e debug # serie a 115200 (solo útil con el entorno debug)
```
