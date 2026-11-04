#include <Bluepad32.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>

// ==========================================
// 1. PINOUT PCB ROBOT (ESP32-S3)
// ==========================================

// Bus I2C Sensores (SDA=7, SCL=15) -> EXACTO AL CÓDIGO QUE TE FUNCIONA
#define SDA_PIN 7
#define SCL_PIN 15

// Pines XSHUT (Sensores)
#define XSHUT_FRONTAL 5
#define XSHUT_IZQ     6
#define XSHUT_DER     4

// Comunicación UART con ESP32-CAM (RX=17, TX=18)
// UART con la camara por la linea GPIO17 (pin 10 de J1) <-> IO14 de la camara (net UART_CAM_TO_ESP
// de la PCB), usada aqui en sentido ESP -> CAM: TX = GPIO17. GPIO18 (-> IO13) queda sin usar. RX apunta a GPIO16 (libre, sin conectar): no se reciben comandos
// de la camara por ahora. No usar -1: el core lo cambia a los pines por defecto (15/16 en el S3).
#define RX_CAM_PIN 16
#define TX_CAM_PIN 17

// LEDs Indicadores
#define LED_FRONTAL 8   // LED Rojo
#define LED_IZQ     9   // LED Amarillo
#define LED_DER     10  // LED Verde

// Driver de Motores TB6612FNG
#define PWMB 1
#define BIN2 2
#define BIN1 42
#define STBY 41
#define AIN1 40
#define AIN2 39
#define PWMA 21

// Configuración Hardware PWM (LEDC)
#define PWM_FREQ   5000
#define PWM_RES    8
#define CANAL_PWMA 0
#define CANAL_PWMB 1

// ==========================================
// 2. PARÁMETROS Y DIRECCIONES I2C
// ==========================================
#define ADDR_FRONTAL 0x30
#define ADDR_IZQ     0x31
#define ADDR_DER     0x32

#define UMBRAL_DISTANCIA_MM 150  // 15 cm
#define VELOCIDAD_BASE      200
#define INTERVALO_ENVIO_MS  800  // periodo de envio de distancias a la ESP32-CAM

// Ajuste del mando
#define ZONA_MUERTA_AVANCE 30   // stick izq. (de 255)
#define ZONA_MUERTA_GIRO   50   // stick der.: mayor para ignorar la deriva
#define GIRO_PORCENTAJE    50   // 100 = giro original, menor = giro mas suave
// Compensacion de motores desiguales (%). Si el robot se desvia a la derecha al
// ir de frente, el motor derecho gira menos: bajar CORRECCION_IZQ (ej. 95).
#define CORRECCION_IZQ     100
#define CORRECCION_DER     100

// Depuracion por serie (115200). Poner en 0 para quitar todos los prints.
#define DEBUG_SERIAL 1
#define DEBUG_INTERVALO_MS 250

Adafruit_VL53L0X lox_frontal = Adafruit_VL53L0X();
Adafruit_VL53L0X lox_izq     = Adafruit_VL53L0X();
Adafruit_VL53L0X lox_der     = Adafruit_VL53L0X();

// Estado de los sensores. Si un sensor no responde al arrancar (ok = false) nunca
// se vuelve a leer por I2C, asi el programa funciona igual sin sensores.
// Distancias en mm; -1 = sin lectura valida (sensor ausente o fuera de rango).
bool okFrontal = false, okIzq = false, okDer = false;
int  distFrontal = -1, distIzq = -1, distDer = -1;

ControllerPtr myControllers[BP32_MAX_GAMEPADS];

// ==========================================
// 3. FUNCIONES DE MOTORES (LEDC)
// ==========================================
void initMotors() {
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);
  pinMode(STBY, OUTPUT);

  digitalWrite(STBY, HIGH);

  ledcSetup(CANAL_PWMA, PWM_FREQ, PWM_RES);
  ledcSetup(CANAL_PWMB, PWM_FREQ, PWM_RES);
  ledcAttachPin(PWMA, CANAL_PWMA);
  ledcAttachPin(PWMB, CANAL_PWMB);
}

void setMotorIzquierdo(int speed) {
  if (speed > 0) {
    digitalWrite(AIN1, HIGH);
    digitalWrite(AIN2, LOW);
  } else if (speed < 0) {
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, HIGH);
    speed = -speed;
  } else {
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, LOW);
  }
  ledcWrite(CANAL_PWMA, constrain(speed, 0, 255));
}

void setMotorDerecho(int speed) {
  if (speed > 0) {
    digitalWrite(BIN1, HIGH);
    digitalWrite(BIN2, LOW);
  } else if (speed < 0) {
    digitalWrite(BIN1, LOW);
    digitalWrite(BIN2, HIGH);
    speed = -speed;
  } else {
    digitalWrite(BIN1, LOW);
    digitalWrite(BIN2, LOW);
  }
  ledcWrite(CANAL_PWMB, constrain(speed, 0, 255));
}

void pararMotores() {
  setMotorIzquierdo(0);
  setMotorDerecho(0);
}

void controlDiferencial(int axisY, int axisRX) {
  int v = map(axisY, -512, 512, 255, -255);
  int w = map(axisRX, -512, 512, -255, 255);

  if (abs(v) < ZONA_MUERTA_AVANCE) v = 0;

  // Giro: zona muerta mayor (absorbe la deriva del stick) y reescalado para que
  // el giro arranque suave desde 0 justo al salir de la zona muerta.
  if (abs(w) < ZONA_MUERTA_GIRO) {
    w = 0;
  } else {
    int signo = (w > 0) ? 1 : -1;
    w = signo * (abs(w) - ZONA_MUERTA_GIRO) * 255 / (255 - ZONA_MUERTA_GIRO);
  }
  // Menor sensibilidad de giro
  w = w * GIRO_PORCENTAJE / 100;

  int izq = v + w;
  int der = v - w;

  // Si una rueda se pasa de 255, se escalan las dos por igual: el recorte
  // por separado reducia el giro justo a maxima velocidad.
  int maximo = max(abs(izq), abs(der));
  if (maximo > 255) {
    izq = izq * 255 / maximo;
    der = der * 255 / maximo;
  }

  // Compensacion de diferencia entre motores (100 = sin correccion)
  izq = izq * CORRECCION_IZQ / 100;
  der = der * CORRECCION_DER / 100;

#if DEBUG_SERIAL
  static unsigned long ultimoDebug = 0;
  if (millis() - ultimoDebug >= DEBUG_INTERVALO_MS) {
    ultimoDebug = millis();
    Serial.printf("[MANDO] axisY=%d axisRX=%d | v=%d w=%d | MotorIzq=%d MotorDer=%d\n",
                  axisY, axisRX, v, w, izq, der);
  }
#endif

  setMotorIzquierdo(izq);
  setMotorDerecho(der);
}

// ==========================================
// 3b. SENSORES VL53L0X (medicion continua, sin bloqueo)
// ==========================================
// Direccion I2C de fabrica de todos los VL53L0X. La biblioteca siempre habla primero
// con ella para cambiarla, asi que si DOS sensores estan vivos a la vez en 0x29,
// el cambio de direccion se aplica a los dos y quedan con la misma direccion.
#define ADDR_DEFECTO 0x29

// true si algun dispositivo contesta (ACK) en esa direccion.
bool respondeI2C(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Apaga (XSHUT en bajo) todos los sensores que todavia no tienen direccion propia.
void apagarSensoresPendientes() {
  if (!okFrontal) digitalWrite(XSHUT_FRONTAL, LOW);
  if (!okIzq)     digitalWrite(XSHUT_IZQ, LOW);
  if (!okDer)     digitalWrite(XSHUT_DER, LOW);
}

// Enciende el sensor (XSHUT), le asigna su direccion I2C y lo deja midiendo solo
// cada 50 ms. Devuelve false si no responde (p. ej. no esta conectado).
// Garantia de direcciones independientes: solo se enciende UN sensor cuando nadie
// mas contesta en 0x29, y si algo falla el sensor se vuelve a apagar para que no
// estorbe al siguiente.
bool iniciarSensor(Adafruit_VL53L0X &lox, uint8_t addr, uint8_t xshut, const char *nombre) {
  // 1. Antes de encender este sensor, 0x29 debe estar libre. Si alguien contesta,
  //    algun sensor no obedece a su XSHUT: se intenta apagar y se vuelve a comprobar.
  if (respondeI2C(ADDR_DEFECTO)) {
    apagarSensoresPendientes();
    delay(50);
    if (respondeI2C(ADDR_DEFECTO)) {
      Serial.printf("[ERROR] Sensor %s: algo contesta en 0x%02X con los XSHUT en bajo "
                    "(un sensor no obedece a su XSHUT). No se asigna direccion para evitar duplicados\n",
                    nombre, ADDR_DEFECTO);
      return false;
    }
  }
  // 2. La direccion destino no puede estar ya ocupada por otro sensor.
  if (respondeI2C(addr)) {
    Serial.printf("[ERROR] Sensor %s: la direccion 0x%02X ya esta ocupada\n", nombre, addr);
    return false;
  }

  // 3. Encender solo este sensor y comprobar que aparece en 0x29.
  digitalWrite(xshut, HIGH);
  delay(30);
  if (!respondeI2C(ADDR_DEFECTO)) {
    Serial.printf("[ERROR] Sensor %s no responde\n", nombre);
    digitalWrite(xshut, LOW);
    return false;
  }

  // 4. Asignar su direccion y verificar que quedo en la nueva y sale de 0x29.
  if (!lox.begin(addr)) {
    Serial.printf("[ERROR] Sensor %s no inicializo\n", nombre);
    digitalWrite(xshut, LOW);
    return false;
  }
  if (!respondeI2C(addr)) {
    Serial.printf("[ERROR] Sensor %s no contesta en 0x%02X tras asignarla\n", nombre, addr);
    digitalWrite(xshut, LOW);
    return false;
  }
  if (respondeI2C(ADDR_DEFECTO)) {
    Serial.printf("[AVISO] Sensor %s: sigue habiendo algo en 0x%02X tras mover el sensor a 0x%02X\n",
                  nombre, ADDR_DEFECTO, addr);
  }

  lox.setMeasurementTimingBudgetMicroSeconds(33000);
  if (!lox.startRangeContinuous(50)) {
    Serial.printf("[ERROR] Sensor %s no pudo iniciar la medicion continua\n", nombre);
    digitalWrite(xshut, LOW);
    return false;
  }
  Serial.printf("[OK] Sensor %s en 0x%02X (medicion continua)\n", nombre, addr);
  return true;
}

// Consulta si el sensor ya tiene una medicion nueva y, si es asi, la guarda en mm.
// No espera: si no hay dato nuevo conserva el ultimo valor y sale al instante.
// Un sensor que no inicializo no se toca (cero trafico I2C).
void actualizarSensor(Adafruit_VL53L0X &lox, bool ok, int &mm) {
  if (!ok) {
    mm = -1;
    return;
  }
  if (lox.isRangeComplete()) {
    uint16_t r = lox.readRangeResult();  // 0xFFFF = fuera de rango / error
    mm = (r == 0xFFFF) ? -1 : (int)r;
  }
}

bool hayObstaculo(int mm) {
  return mm > 0 && mm <= UMBRAL_DISTANCIA_MM;
}

// ==========================================
// 4. CALLBACKS BLUEPAD32
// ==========================================
void processGamepad(ControllerPtr ctl) {
  int axisY = ctl->axisY();
  int axisRX = ctl->axisRX();
  controlDiferencial(axisY, axisRX);
}

void onConnectedController(ControllerPtr ctl) {
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == nullptr) {
      Serial.printf(">> Mando Xbox Conectado en indice: %d\n", i);
      myControllers[i] = ctl;
      break;
    }
  }
}

void onDisconnectedController(ControllerPtr ctl) {
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == ctl) {
      Serial.printf(">> Mando Xbox Desconectado del indice: %d\n", i);
      myControllers[i] = nullptr;
      break;
    }
  }
}

// ==========================================
// 5. SETUP Y LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
#if DEBUG_SERIAL && ARDUINO_USB_CDC_ON_BOOT
  // Con el serial por USB nativo, espera (max. 3 s) a que se abra el monitor para no perder los primeros mensajes
  { unsigned long t0 = millis(); while (!Serial && millis() - t0 < 3000) delay(10); }
#endif
  delay(1000);

  // Serial1: Comunicación UART con ESP32-CAM
  Serial1.begin(115200, SERIAL_8N1, RX_CAM_PIN, TX_CAM_PIN);

  Serial.println("\n--- INICIALIZANDO SISTEMA COMPLETO ---");

  // 1. Configurar LEDs y autotest al encender: los 3 LED encendidos 500 ms
  //    (comprobar que los LED funcionan). Despues, tras iniciar los sensores,
  //    se encienden 500 ms solo los LED de los sensores que si funcionan.
  pinMode(LED_FRONTAL, OUTPUT);
  pinMode(LED_IZQ, OUTPUT);
  pinMode(LED_DER, OUTPUT);

  digitalWrite(LED_FRONTAL, HIGH);
  digitalWrite(LED_IZQ, HIGH);
  digitalWrite(LED_DER, HIGH);
#if DEBUG_SERIAL
  // Nivel REAL de cada pin (los GPIO son entrada/salida): 1 = el ESP32 esta entregando 3.3 V,
  // 0 = algo lo esta forzando a GND (corto) aunque el firmware haya puesto HIGH.
  Serial.printf("[LED-TEST] t=%lums GPIO HIGH -> leido: ROJO(GPIO%d)=%d AMARILLO(GPIO%d)=%d VERDE(GPIO%d)=%d\n",
                millis(), LED_FRONTAL, digitalRead(LED_FRONTAL), LED_IZQ, digitalRead(LED_IZQ),
                LED_DER, digitalRead(LED_DER));
#endif
  delay(500);
#if DEBUG_SERIAL
  Serial.printf("[LED-TEST] t=%lums tras 500 ms, aun HIGH: ROJO=%d AMARILLO=%d VERDE=%d\n",
                millis(), digitalRead(LED_FRONTAL), digitalRead(LED_IZQ), digitalRead(LED_DER));
#endif
  digitalWrite(LED_FRONTAL, LOW);
  digitalWrite(LED_IZQ, LOW);
  digitalWrite(LED_DER, LOW);

  // 2. Inicializar Motores
  initMotors();
  pararMotores();

  // 3. Configurar pines XSHUT y apagar sensores para reset
  pinMode(XSHUT_FRONTAL, OUTPUT);
  pinMode(XSHUT_IZQ, OUTPUT);
  pinMode(XSHUT_DER, OUTPUT);

  digitalWrite(XSHUT_FRONTAL, LOW);
  digitalWrite(XSHUT_IZQ, LOW);
  digitalWrite(XSHUT_DER, LOW);
  delay(50);

  // 4. Iniciar bus I2C (SDA=7, SCL=15)
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000); // 100 kHz para estabilidad con Bluetooth
  delay(50);

  // Sensores: uno a uno (cada uno toma su direccion al encenderse)
  okFrontal = iniciarSensor(lox_frontal, ADDR_FRONTAL, XSHUT_FRONTAL, "Frontal");
  okIzq     = iniciarSensor(lox_izq,     ADDR_IZQ,     XSHUT_IZQ,     "Izquierdo");
  okDer     = iniciarSensor(lox_der,     ADDR_DER,     XSHUT_DER,     "Derecho");

  // Cada sensor es independiente: los que fallan quedan apagados (-1 para LED y
  // camara) y los demas siguen funcionando.
  Serial.printf("Sensores: Frontal=%s Izquierdo=%s Derecho=%s\n",
                okFrontal ? "OK" : "FALLA", okIzq ? "OK" : "FALLA", okDer ? "OK" : "FALLA");

  // Segundo autotest: 500 ms con encendidos solo los LED de los sensores que funcionan
  digitalWrite(LED_FRONTAL, okFrontal ? HIGH : LOW);
  digitalWrite(LED_IZQ,     okIzq     ? HIGH : LOW);
  digitalWrite(LED_DER,     okDer     ? HIGH : LOW);
  delay(500);
  digitalWrite(LED_FRONTAL, LOW);
  digitalWrite(LED_IZQ, LOW);
  digitalWrite(LED_DER, LOW);

  // 5. Inicializar servicio Bluetooth Bluepad32
  BP32.setup(&onConnectedController, &onDisconnectedController);

  Serial.println("--------------------------------\n");
}

void loop() {
#if DEBUG_SERIAL
  // Duracion de la vuelta anterior del loop (debe ser ~20 ms)
  static unsigned long tAnterior = 0;
  unsigned long duracionLoop = millis() - tAnterior;
  tAnterior = millis();
#endif
  // 1. Sensores VL53L0X: solo se recoge la ultima medicion (no bloquea)
  actualizarSensor(lox_frontal, okFrontal, distFrontal);
  actualizarSensor(lox_izq,     okIzq,     distIzq);
  actualizarSensor(lox_der,     okDer,     distDer);

  // EVALUACIÓN Y CONTROL DE LEDS
  bool frontDetectado = hayObstaculo(distFrontal);
  bool izqDetectado   = hayObstaculo(distIzq);
  bool derDetectado   = hayObstaculo(distDer);
  digitalWrite(LED_FRONTAL, frontDetectado ? HIGH : LOW);
  digitalWrite(LED_IZQ,     izqDetectado   ? HIGH : LOW);
  digitalWrite(LED_DER,     derDetectado   ? HIGH : LOW);

  // 2. Procesar Mando Bluetooth (Bluepad32)
  BP32.update();
  bool mandoConectado = false;

  for (auto ctl : myControllers) {
    if (ctl && ctl->isConnected()) {
      processGamepad(ctl);
      mandoConectado = true;
    }
  }

  // 3. Procesar Comandos UART desde ESP32-CAM (si el mando no está conectado)
  if (!mandoConectado && Serial1.available()) {
    String comando = Serial1.readStringUntil('\n');
    comando.trim();
    comando.toUpperCase();

    if (comando == "F" || comando == "ADELANTE") {
      setMotorIzquierdo(VELOCIDAD_BASE);
      setMotorDerecho(VELOCIDAD_BASE);
    } 
    else if (comando == "B" || comando == "ATRAS") {
      setMotorIzquierdo(-VELOCIDAD_BASE);
      setMotorDerecho(-VELOCIDAD_BASE);
    } 
    else if (comando == "L" || comando == "IZQUIERDA") {
      setMotorIzquierdo(-VELOCIDAD_BASE);
      setMotorDerecho(VELOCIDAD_BASE);
    } 
    else if (comando == "R" || comando == "DERECHA") {
      setMotorIzquierdo(VELOCIDAD_BASE);
      setMotorDerecho(-VELOCIDAD_BASE);
    } 
    else if (comando == "S" || comando == "PARAR") {
      pararMotores();
    }
  }

  // 4. Enviar distancias a la ESP32-CAM cada INTERVALO_ENVIO_MS: "D,frente,izq,der\n" (mm, -1 = sin lectura)
  static unsigned long ultimoEnvio = 0;
  if (millis() - ultimoEnvio >= INTERVALO_ENVIO_MS) {
    ultimoEnvio = millis();
    Serial1.printf("D,%d,%d,%d\n", distFrontal, distIzq, distDer);
  }

#if DEBUG_SERIAL
  static unsigned long ultimoDebugSensores = 0;
  if (millis() - ultimoDebugSensores >= DEBUG_INTERVALO_MS) {
    ultimoDebugSensores = millis();
    Serial.printf("[SENS] F: ok=%d mm=%d | I: ok=%d mm=%d | D: ok=%d mm=%d | LEDs F=%d I=%d D=%d | loop=%lums mando=%d\n",
                  okFrontal, distFrontal, okIzq, distIzq, okDer, distDer,
                  frontDetectado, izqDetectado, derDetectado,
                  duracionLoop, mandoConectado);
  }
#endif

  delay(20);
}


