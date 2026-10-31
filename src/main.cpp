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
#define RX_CAM_PIN 17
#define TX_CAM_PIN 18

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

Adafruit_VL53L0X lox_frontal = Adafruit_VL53L0X();
Adafruit_VL53L0X lox_izq     = Adafruit_VL53L0X();
Adafruit_VL53L0X lox_der     = Adafruit_VL53L0X();

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

  if (abs(v) < 30) v = 0;
  if (abs(w) < 30) w = 0;

  int izq = v + w;
  int der = v - w;

  setMotorIzquierdo(izq);
  setMotorDerecho(der);
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
  delay(1000);

  // Serial1: Comunicación UART con ESP32-CAM
  Serial1.begin(115200, SERIAL_8N1, RX_CAM_PIN, TX_CAM_PIN);

  Serial.println("\n--- INICIALIZANDO SISTEMA COMPLETO ---");

  // 1. Configurar LEDs y autotest al encender
  pinMode(LED_FRONTAL, OUTPUT);
  pinMode(LED_IZQ, OUTPUT);
  pinMode(LED_DER, OUTPUT);

  digitalWrite(LED_FRONTAL, HIGH);
  digitalWrite(LED_IZQ, HIGH);
  digitalWrite(LED_DER, HIGH);
  delay(500);
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

  // Sensor Frontal
  digitalWrite(XSHUT_FRONTAL, HIGH);
  delay(30);
  if (lox_frontal.begin(ADDR_FRONTAL)) {
    Serial.println("[OK] Sensor Frontal en 0x30");
    lox_frontal.setMeasurementTimingBudgetMicroSeconds(33000);
  } else {
    Serial.println("[ERROR] Sensor Frontal no responde");
  }

  // Sensor Izquierdo
  digitalWrite(XSHUT_IZQ, HIGH);
  delay(30);
  if (lox_izq.begin(ADDR_IZQ)) {
    Serial.println("[OK] Sensor Izquierdo en 0x31");
    lox_izq.setMeasurementTimingBudgetMicroSeconds(33000);
  } else {
    Serial.println("[ERROR] Sensor Izquierdo no responde");
  }

  // Sensor Derecho
  digitalWrite(XSHUT_DER, HIGH);
  delay(30);
  if (lox_der.begin(ADDR_DER)) {
    Serial.println("[OK] Sensor Derecho en 0x32");
    lox_der.setMeasurementTimingBudgetMicroSeconds(33000);
  } else {
    Serial.println("[ERROR] Sensor Derecho no responde");
  }

  // 5. Inicializar servicio Bluetooth Bluepad32
  BP32.setup(&onConnectedController, &onDisconnectedController);

  Serial.println("--------------------------------\n");
}

void loop() {
  // 1. Lectura de Sensores VL53L0X
  VL53L0X_RangingMeasurementData_t mFront, mLeft, mRight;
  lox_frontal.rangingTest(&mFront, false);
  lox_izq.rangingTest(&mLeft, false);
  lox_der.rangingTest(&mRight, false);

  // EVALUACIÓN Y CONTROL DE LEDS
  bool frontDetectado = (mFront.RangeStatus != 4) && (mFront.RangeMilliMeter > 0) && (mFront.RangeMilliMeter <= UMBRAL_DISTANCIA_MM);
  digitalWrite(LED_FRONTAL, frontDetectado ? HIGH : LOW);

  bool izqDetectado = (mLeft.RangeStatus != 4) && (mLeft.RangeMilliMeter > 0) && (mLeft.RangeMilliMeter <= UMBRAL_DISTANCIA_MM);
  digitalWrite(LED_IZQ, izqDetectado ? HIGH : LOW);

  bool derDetectado = (mRight.RangeStatus != 4) && (mRight.RangeMilliMeter > 0) && (mRight.RangeMilliMeter <= UMBRAL_DISTANCIA_MM);
  digitalWrite(LED_DER, derDetectado ? HIGH : LOW);

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

  delay(20);
}


