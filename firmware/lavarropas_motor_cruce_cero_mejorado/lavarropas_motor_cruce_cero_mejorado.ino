/*
  LAVARROPAS - MOTOR CON CRUCE POR CERO (PID MEJORADO) - Arduino UNO
  -----------------------------------------------------------------
  Versión mejorada del control standalone del motor con TRIAC, según el
  código entregado por el profesor (respuesta.txt).

  Diferencias respecto de lavarropas_motor_cruce_cero.ino (original):
    - Kp = 0.0025 (más reacción a errores grandes)
    - Ki = 0.0005 (precisión a largo plazo)
    - Filtro de RPM suavizado: rpm = 0.85*rpm + 0.15*rpmNueva
    - Anti-windup: el integral solo se acumula si |error| < 2000 RPM
    - Rampa dinámica de potencia según el error:
        2% por ciclo  (error pequeño)
        8% por ciclo  (|error| > 1500 RPM)
       15% por ciclo  (|error| > 3000 RPM)

  Salida por monitor serie en TEXTO PLANO a 115200 baudios (NO emite JSON).
  Comandos:
    R<rpm>   -> lazo PID con setpoint en RPM (500..12000)
    P<0-100> -> modo manual (potencia fija en %)
    R0       -> parar

  Pines (UNO):
    D2        cruce por cero (PC817 / H11AA1, flanco ascendente)
    D3        disparo del MOC3021 hacia el TRIAC
    D6 (AIN0) entrada del tacómetro (comparador analógico interno)
    D7 (AIN1) referencia 2,5 V del tacómetro
*/

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

// PID (Ajustados para respuesta rápida y estabilidad)
float Kp = 0.0025;   // Aumentado para que reaccione con más fuerza a errores grandes
float Ki = 0.0005;   // Mantiene la precisión a largo plazo
const float POT_ARRANQUE = 20.0;   // % inicial al pasar a PID desde parado
const float POT_MAX_PID  = 60.0;   // tope de seguridad en lazo cerrado
const unsigned long DT_MS = 100;   // período del PID
const int MAX_VENTANAS_SIN_TACO = 20;  // 2 s sin pulsos -> corte

// Variables para control de rampa
float potenciaAnterior = 0;

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
  potenciaAnterior = 0;
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
        potenciaAnterior = potencia;
      }
      Serial.print("Setpoint: ");
      Serial.print(setpoint, 0);
      Serial.println(" RPM");
    }
  } else if (cmd == 'P') {
    modoPID = false;
    integral = 0;
    aplicarPotencia(constrain(v, 0, 100));
    potenciaAnterior = potencia;
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
    
    // FILTRO SUAVIZADO
    rpm = 0.85 * rpm + 0.15 * rpmNueva;

    if (modoPID) {
      // Seguridad: sin señal del tacómetro no hay lazo
      if (c < 2) ventanasSinTaco++; else ventanasSinTaco = 0;
      
      if (ventanasSinTaco >= MAX_VENTANAS_SIN_TACO) {
        parar();
        Serial.println("SIN SENAL DE TACOMETRO: motor parado");
      } else {
        float error = setpoint - rpm;
        
        // ANTI-WINDUP: Solo acumula el integral si el error es menor a 2000 RPM.
        // Así evitamos que el integral se sature al dar un salto grande (ej. de 5000 a 10000)
        if (abs(error) < 2000) { 
          integral += Ki * error * (DT_MS / 1000.0);
          integral = constrain(integral, 0.0, POT_MAX_PID);
        }
        
        float salida = Kp * error + integral;
        
        // RAMPA DINÁMICA: 
        // Si el error es grande (> 1500 RPM), permitimos que la potencia suba muy rápido (15% por ciclo)
        // Si el error es pequeño, la rampa es suave (2% por ciclo) para no pasarse de vueltas.
        float limiteRampa = 2.0; 
        if (abs(error) > 3000) {
          limiteRampa = 15.0; // Aceleración agresiva
        } else if (abs(error) > 1500) {
          limiteRampa = 8.0;  // Aceleración moderada
        }
        
        salida = constrain(salida, potenciaAnterior - limiteRampa, potenciaAnterior + limiteRampa);
        
        // Aplicar y guardar el valor para el próximo ciclo
        aplicarPotencia(constrain(salida, 0.0, POT_MAX_PID));
        potenciaAnterior = potencia; 
      }
    } else {
      potenciaAnterior = potencia; // Mantener sincronizado en modo manual
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
