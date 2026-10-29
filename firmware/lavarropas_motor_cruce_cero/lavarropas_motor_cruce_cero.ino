const int PIN_ZC  = 2;   // cruce por cero
const int PIN_MOC = 3;   // disparo MOC3021

// Disparo
const unsigned long RETARDO_MIN = 500;     // us -> potencia máxima
const unsigned long RETARDO_MAX = 9300;    // us -> potencia mínima
const unsigned long PULSO_US    = 1000;    // pulso de disparo

// Tacómetro
const unsigned long MIN_INTERVALO_US = 250;
const unsigned long BLOQUEO_US       = 400;
const int PULSOS_POR_VUELTA = 8;

// PID (se ajustan al probar)
float Kp = 0.001;   // % por cada RPM de error
float Ki = 0.003;   // % por cada RPM de error por segundo
const float POT_ARRANQUE = 20.0;   // % inicial al pasar a PID desde parado
const float POT_MAX_PID  = 60.0;   // tope de seguridad en lazo cerrado
const unsigned long DT_MS = 100;   // período del PID
const int MAX_VENTANAS_SIN_TACO = 20;  // 2 s sin pulsos -> corte

volatile unsigned long tCruce = 0;
volatile bool nuevoCruce = false;
volatile unsigned long tDisparo = 0;
volatile unsigned long tUltimoPulso = 0;
volatile unsigned long tPrimero = 0;
volatile unsigned long cuentaTaco = 0;

float potencia = 0;        // % que se aplica
float setpoint = 0;        // RPM objetivo
float integral = 0;        // término integral, en %
float rpm = 0;             // RPM filtradas
bool modoPID = false;
int ventanasSinTaco = 0;
bool disparado = true;
unsigned long retardo = RETARDO_MAX;
unsigned long tPID = 0, tPrint = 0;

char buf[16];
byte nBuf = 0;

void cruceCero() {
  tCruce = micros();
  nuevoCruce = true;
}

ISR(ANALOG_COMP_vect) {
  unsigned long ahora = micros();
  if (ahora - tDisparo < BLOQUEO_US) return;
  if (ahora - tUltimoPulso < MIN_INTERVALO_US) return;
  tUltimoPulso = ahora;
  if (cuentaTaco == 0) tPrimero = ahora;
  cuentaTaco++;
}

void aplicarPotencia(float p) {
  potencia = constrain(p, 0.0, 100.0);
  retardo = RETARDO_MAX - (unsigned long)((RETARDO_MAX - RETARDO_MIN) * potencia / 100.0);
}

void parar() {
  modoPID = false;
  setpoint = 0;
  integral = 0;
  aplicarPotencia(0);
}

void procesar(char* s) {
  char cmd = toupper(s[0]);
  int v = atoi(s + 1);
  if (cmd == 'R') {
    if (v <= 0) {
      parar();
      Serial.println("PARADO");
    } else {
      setpoint = constrain(v, 500, 12000);
      if (!modoPID) {
        modoPID = true;
        ventanasSinTaco = 0;
        integral = (potencia > POT_ARRANQUE) ? potencia : POT_ARRANQUE;
        aplicarPotencia(integral);
      }
      Serial.print("Setpoint: ");
      Serial.print(setpoint, 0);
      Serial.println(" RPM");
    }
  } else if (cmd == 'P') {
    modoPID = false;
    integral = 0;
    aplicarPotencia(constrain(v, 0, 100));
    Serial.print("Manual: ");
    Serial.print(potencia, 0);
    Serial.println(" %");
  }
}

void leerSerie() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (nBuf > 0) {
        buf[nBuf] = 0;
        procesar(buf);
        nBuf = 0;
      }
    } else if (nBuf < sizeof(buf) - 1) {
      buf[nBuf++] = ch;
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_MOC, OUTPUT);
  digitalWrite(PIN_MOC, LOW);
  pinMode(PIN_ZC, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ZC), cruceCero, RISING);

  // Comparador analógico: AIN0 (D6) vs AIN1 (D7)
  DIDR1 = _BV(AIN1D) | _BV(AIN0D);
  ACSR  = _BV(ACIE) | _BV(ACIS1) | _BV(ACIS0);

  tPID = millis();
  tPrint = millis();
  Serial.println("R<rpm> = PID | P<0-100> = manual | R0 = parar");
}

void loop() {
  leerSerie();

  // Disparo del TRIAC
  noInterrupts();
  unsigned long t0 = tCruce;
  bool n = nuevoCruce;
  nuevoCruce = false;
  interrupts();

  if (n) disparado = false;

  if (!disparado && potencia > 0 && (micros() - t0) >= retardo) {
    noInterrupts();
    tDisparo = micros();
    interrupts();
    digitalWrite(PIN_MOC, HIGH);
    delayMicroseconds(PULSO_US);
    digitalWrite(PIN_MOC, LOW);
    disparado = true;
  }

  // Medición de RPM + PID cada DT_MS
  if (millis() - tPID >= DT_MS) {
    tPID += DT_MS;

    noInterrupts();
    unsigned long c = cuentaTaco;
    unsigned long t1 = tPrimero;
    unsigned long t2 = tUltimoPulso;
    cuentaTaco = 0;
    interrupts();

    float rpmNueva = 0;
    if (c >= 2 && t2 > t1) {
      rpmNueva = (c - 1) * 1000000.0 / (float)(t2 - t1) * 60.0 / PULSOS_POR_VUELTA;
    }
    rpm = 0.5 * rpm + 0.5 * rpmNueva;

    if (modoPID) {
      // Seguridad: sin señal del tacómetro no hay lazo
      if (c < 2) ventanasSinTaco++; else ventanasSinTaco = 0;
      if (ventanasSinTaco >= MAX_VENTANAS_SIN_TACO) {
        parar();
        Serial.println("SIN SENAL DE TACOMETRO: motor parado");
      } else {
        float error = setpoint - rpm;
        integral += Ki * error * (DT_MS / 1000.0);
        integral = constrain(integral, 0.0, POT_MAX_PID);
        float salida = Kp * error + integral;
        aplicarPotencia(constrain(salida, 0.0, POT_MAX_PID));
      }
    }
  }

  // Informe cada 500 ms
  if (millis() - tPrint >= 500) {
    tPrint += 500;
    Serial.print("SP: ");
    Serial.print(setpoint, 0);
    Serial.print("   RPM: ");
    Serial.print(rpm, 0);
    Serial.print("   Pot: ");
    Serial.print(potencia, 1);
    Serial.println(" %");
  }
}