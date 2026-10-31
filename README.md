# esp32-robot

Firmware del ESP32-S3 principal del robot de fútbol **auxi**. Maneja los motores
con un mando Xbox por Bluetooth, lee tres sensores de distancia y recibe
comandos por UART desde la ESP32-CAM.

## Requisitos

- [PlatformIO](https://platformio.org/)
- ESP32-S3-DevKitC-1 (N16R8) en la PCB `auxi`

## Compilar y subir

```
pio run --target upload
pio device monitor
```

## Control

- **Mando Xbox (BLE):** stick izquierdo vertical = avance, stick derecho horizontal = giro.
- **UART desde la ESP32-CAM** (solo si no hay mando): `F`, `B`, `L`, `R`, `S`.

Más detalles en [CLAUDE.md](CLAUDE.md).
