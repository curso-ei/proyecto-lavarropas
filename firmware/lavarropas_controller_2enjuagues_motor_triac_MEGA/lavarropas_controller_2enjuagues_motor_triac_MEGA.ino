/*
  CONTROLADOR DE LAVARROPAS - Arduino MEGA 2560
  Maquina de estados no bloqueante (millis(), sin delay())
  Display LCD 16x2 en modo paralelo (4 bits), Monitor Serie (JSON para el
  dashboard) y Alarma Sonora.

  VARIANTE: 2 ENJUAGUES + MOTOR CON TRIAC (cruce por cero + tacometro + PID)
  ----------------------------------------------------------------
  Une dos sketches:
    1) lavarropas_controller_display_error_buzzer_2enjuagues.ino  (ciclo)
    2) lavarropas_motor_cruce_cero.txt                            (motor)

  TODOS LOS PINES DEL CICLO ORIGINAL QUEDAN IGUAL (LCD, START, presostato,
  traba, buzzer, reles). Solo se agregan 3 pines para el motor:
    D19 = cruce por cero (INT2)   D18 = tacometro (INT3)   D22 = disparo MOC3021

  IMPORTANTE - TACOMETRO EN EL MEGA:
    El sketch del motor usaba el comparador analogico con AIN0 (D6) y AIN1 (D7)
    del Uno. En la placa Mega el pin AIN0 (PE2) NO sale a ningun conector, asi
    que ese circuito no se puede conectar igual. Aca el tacometro entra por una
    INTERRUPCION EXTERNA (D18) y necesita una senal DIGITAL 0-5 V. Si tu
    tacometro entrega una senal analogica/alterna, hace falta un comparador
    externo (LM393 o similar) entre el tacometro y D18.

  Que cambia respecto del ciclo original:
    - Los reles CW/CCW solo ELIGEN EL SENTIDO (se conmutan con el TRIAC apagado);
      el TRIAC da la potencia y un PID sobre el tacometro mantiene las RPM.
    - Agitado (LAVADO / ENJUAGUE / ENJUAGUE_SUAVIZANTE): mismo vaiven, a RPM_AGITADO.
    - CENTRIFUGADO: lo hace el mismo motor con TRIAC + PID a RPM_CENTRIFUGADO
      (con rampa). El rele PIN_CENTRIFUGA queda sin uso; se puede volver al
      comportamiento original poniendo CENTRIFUGA_POR_MOTOR = false.
    - Fase nueva ERROR_MOTOR: si el tacometro no da pulsos, se corta todo.
    - El disparo del TRIAC se hace con Timer1 (interrupcion), no desde loop():
      el JSON por Serial y el LCD bloquean el loop y harian perder disparos.
    - Proteccion por "latido": si loop() se cuelga, el TRIAC deja de disparar.
    - La traba de puerta no se suelta hasta que el tacometro indica tambor parado.
    - El JSON mantiene los mismos campos y orden; se agrega el objeto "motor".
*/

#include <LiquidCrystal.h>
#include <ArduinoJson.h>   // reporte de estado en JSON para el dashboard

// #define DEBUG_LCD_SERIAL   // Descomentar para volcar el texto del LCD por Serial.

// ---------- LCD ----------
LiquidCrystal lcd(A2, 6, A3, 5, A0, A1);

// ---------- Pines de entrada (iguales al original) ----------
const uint8_t PIN_START      = 2;
const uint8_t PIN_PRESOSTATO = 3;

// ---------- Pines del motor (NUEVOS; interrupciones externas del Mega: 2,3,18,19,20,21) ----------
const uint8_t PIN_ZC   = 19;  // cruce por cero (INT2)
const uint8_t PIN_TACO = 18;  // tacometro, senal digital (INT3)
const uint8_t PIN_MOC  = 22;  // disparo MOC3021

// ---------- Pines de salida (reles y alarmas; iguales al original) ----------
const uint8_t PIN_TRABA       = 4;
const uint8_t PIN_BUZZER      = 7;  // Buzzer conectado al Pin 7
const uint8_t PIN_LLENADO     = 8;
const uint8_t PIN_MOTOR_CW    = 9;
const uint8_t PIN_MOTOR_CCW   = 10;
const uint8_t PIN_DRENAJE     = 11;
const uint8_t PIN_SUAVIZANTE  = 12;
const uint8_t PIN_CENTRIFUGA  = 13;

const bool RELAY_ACTIVE_LOW = true;

// ---------- Fases del ciclo ----------
enum Fase {
  REPOSO,
  LLENADO,
  LAVADO,
  DRENAJE_1,
  LLENADO_ENJUAGUE,
  ENJUAGUE,
  DRENAJE_2,
  LLENADO_SUAVIZANTE,   // carga agua + suavizante
  ENJUAGUE_SUAVIZANTE,  // mueve la ropa para mezclar el suavizante
  DRENAJE_3,
  CENTRIFUGADO,
  FIN,
  ERROR_AGUA,
  ERROR_MOTOR           // nuevo: sin senal del tacometro
};

Fase faseActual = REPOSO;
Fase fasePreviaAlError = REPOSO;

// ---------- Temporizados de fases fijas ----------
const unsigned long T_DRENAJE       = 10000UL;
const unsigned long T_CENTRIFUGADO  = 15000UL;

// Inversión de giro del motor
const unsigned long T_GIRO = 5000UL;
const unsigned long T_PAUSA_GIRO = 2000UL;

// ---------- Control por cantidad de ciclos ----------
const int MAX_GIROS = 3;
int girosContados = 0;

// ---------- Variables de control de tiempo ----------
unsigned long tInicioFase = 0;
unsigned long tUltimoGiro = 0;
unsigned long tErrorMensaje = 0;
bool motorSentidoCW = true;
bool motorActivo = false;       // true mientras el motor esta en un "giro"
bool alternarTextoError = true;

// ---------- Reporte de estado al dashboard ----------
unsigned long tUltimoEnvioEstado = 0;
const unsigned long INTERVALO_ENVIO_MS = 1000;  // 1 línea JSON por segundo
bool pulsadorActivo = false;

// ---------- Antirrebote del botón Start ----------
unsigned long tUltimoCambioBoton = 0;
const unsigned long DEBOUNCE_MS = 50;
bool estadoBotonAnterior = HIGH;

String ultimaLinea1 = "";
String ultimaLinea2 = "";

// =====================================================================
//                         MOTOR CON TRIAC
// =====================================================================

// ---------- Disparo (ajustes del sketch del motor) ----------
const unsigned long RETARDO_MIN = 500;     // us -> potencia máxima
const unsigned long RETARDO_MAX = 9300;    // us -> potencia mínima
const unsigned long PULSO_US    = 1000;    // pulso de disparo
const uint16_t PULSO_TICKS = PULSO_US * 2; // Timer1: prescaler 8 -> 0,5 us por tick

// ---------- Tacómetro ----------
const unsigned long MIN_INTERVALO_US = 250;
const unsigned long BLOQUEO_US       = 400;
const int PULSOS_POR_VUELTA = 8;

// ---------- PID (se ajustan al probar, con el sketch del motor solo) ----------
float Kp = 0.001;   // % por cada RPM de error
float Ki = 0.003;   // % por cada RPM de error por segundo
const float POT_ARRANQUE = 20.0;   // % inicial al pasar a PID desde parado
const float POT_MAX_PID  = 60.0;   // tope de seguridad en lazo cerrado
const unsigned long DT_MS = 100;   // período del PID
const unsigned long T_SIN_TACO_MS = 2000;  // sin pulsos durante este tiempo -> falla

// ---------- Velocidades por fase (AJUSTAR en la maquina real) ----------
const float RPM_MIN = 500.0;               // mínimo que acepta el PID
const float RPM_MAX = 12000.0;             // máximo que acepta el PID
const float RPM_AGITADO       = 600.0;     // vaivén de lavado/enjuague
const float RPM_CENTRIFUGADO  = 6000.0;    // centrifugado
const float RAMPA_RPM_S       = 1000.0;    // subida del setpoint, RPM por segundo
// false = centrifuga con el rele PIN_CENTRIFUGA, como el original.
// true  = centrifuga el propio motor con TRIAC + PID (a RPM_CENTRIFUGADO, con rampa).
const bool  CENTRIFUGA_POR_MOTOR = true;
const bool  SENTIDO_CENTRIFUGADO_CW = true;
const float RPM_PUERTA_SEGURA = 100.0;     // la puerta se libera cuando el tacometro marca menos que esto

// ---------- Secuencia de reles ----------
const unsigned long T_RELE_MS = 100;       // espera entre conmutar el rele y dar/cortar potencia

// ---------- Latido (proteccion si loop() se cuelga) ----------
const unsigned long T_LATIDO_MAX_MS = 800; // sin latido por mas de esto -> el TRIAC no dispara

enum MotorEstado { M_PARADO, M_ARRANQUE, M_MARCHA, M_PARANDO };
MotorEstado motorEstado = M_PARADO;
unsigned long tMotorEstado = 0;

// Compartidas con las interrupciones
volatile uint16_t retardoTicks = 0;
volatile bool disparoHabilitado = false;
volatile uint8_t faseDisparo = 0;          // 0 = esperando disparo, 1 = pulso en curso
volatile unsigned long tDisparo = 0;
volatile unsigned long tLatido = 0;
volatile unsigned long tUltimoPulso = 0;
volatile unsigned long tPrimero = 0;
volatile unsigned long cuentaTaco = 0;

float potencia = 0;        // % que se aplica
float setpoint = 0;        // RPM objetivo instantáneo (con rampa)
float setpointFinal = 0;   // RPM objetivo final
float integral = 0;        // término integral, en %
float rpm = 0;             // RPM filtradas
unsigned long msSinTaco = 0;
bool falloTacometro = false;
unsigned long tPID = 0;

// ---------- Interrupciones ----------

// Cruce por cero: arma el Timer1 para disparar despues de "retardoTicks".
void cruceCero() {
  digitalWrite(PIN_MOC, LOW);   // por si quedo un pulso a medias
  TCCR1B = 0;                   // detiene el timer
  TIMSK1 = 0;
  if (!disparoHabilitado) return;
  if ((millis() - tLatido) > T_LATIDO_MAX_MS) {   // loop() colgado: no disparar
    disparoHabilitado = false;
    return;
  }
  TCNT1 = 0;
  OCR1A = retardoTicks;
  faseDisparo = 0;
  TIFR1  = _BV(OCF1A);          // borra una coincidencia pendiente
  TIMSK1 = _BV(OCIE1A);
  TCCR1B = _BV(CS11);           // prescaler 8, arranca
}

// Coincidencia de Timer1: primero sube el pulso, despues lo baja.
ISR(TIMER1_COMPA_vect) {
  if (faseDisparo == 0) {
    tDisparo = micros();
    digitalWrite(PIN_MOC, HIGH);
    faseDisparo = 1;
    OCR1A += PULSO_TICKS;
  } else {
    digitalWrite(PIN_MOC, LOW);
    TIMSK1 = 0;
    TCCR1B = 0;
  }
}

// Pulsos del tacometro (interrupcion externa, flanco ascendente)
void pulsoTaco() {
  unsigned long ahora = micros();
  if (ahora - tDisparo < BLOQUEO_US) return;
  if (ahora - tUltimoPulso < MIN_INTERVALO_US) return;
  tUltimoPulso = ahora;
  if (cuentaTaco == 0) tPrimero = ahora;
  cuentaTaco++;
}

// ================= FUNCIONES AUXILIARES =================

void setRelay(uint8_t pin, bool encender) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(pin, encender ? LOW : HIGH);
  } else {
    digitalWrite(pin, encender ? HIGH : LOW);
  }
}

// ---------- Motor: funciones ----------

void triacApagar() {
  noInterrupts();
  disparoHabilitado = false;
  TIMSK1 = 0;
  TCCR1B = 0;
  digitalWrite(PIN_MOC, LOW);
  interrupts();
}

void aplicarPotencia(float p) {
  potencia = constrain(p, 0.0, 100.0);
  unsigned long retardo = RETARDO_MAX - (unsigned long)((RETARDO_MAX - RETARDO_MIN) * potencia / 100.0);
  uint16_t ticks = (uint16_t)(retardo * 2UL);
  noInterrupts();
  retardoTicks = ticks;
  disparoHabilitado = (potencia > 0.0);
  interrupts();
}

// Arranca el motor: primero elige el sentido con los reles (sin potencia),
// y recien despues de T_RELE_MS actualizarMotor() habilita el TRIAC.
// Devuelve false si el motor todavia no esta del todo parado.
bool motorIniciar(bool cw, float rpmObjetivo) {
  if (motorEstado != M_PARADO) return false;
  setRelay(PIN_MOTOR_CW,  cw);
  setRelay(PIN_MOTOR_CCW, !cw);
  setpointFinal = constrain(rpmObjetivo, RPM_MIN, RPM_MAX);
  motorEstado = M_ARRANQUE;
  tMotorEstado = millis();
  motorActivo = true;
  return true;
}

// Corta la potencia YA y suelta los reles T_RELE_MS despues.
void motorDetener() {
  triacApagar();
  potencia = 0;
  integral = 0;
  setpoint = 0;
  motorActivo = false;
  if (motorEstado == M_ARRANQUE || motorEstado == M_MARCHA) {
    motorEstado = M_PARANDO;
    tMotorEstado = millis();
  }
}

// Se llama en cada pasada de loop(), en cualquier fase.
void actualizarMotor() {
  unsigned long ahora = millis();

  // 1) Secuencia de reles
  if (motorEstado == M_ARRANQUE && (ahora - tMotorEstado) >= T_RELE_MS) {
    noInterrupts();
    cuentaTaco = 0;
    interrupts();
    msSinTaco = 0;
    setpoint = RPM_MIN;
    integral = POT_ARRANQUE;
    aplicarPotencia(integral);
    tPID = ahora;
    motorEstado = M_MARCHA;
  } else if (motorEstado == M_PARANDO && (ahora - tMotorEstado) >= T_RELE_MS) {
    setRelay(PIN_MOTOR_CW, false);
    setRelay(PIN_MOTOR_CCW, false);
    motorEstado = M_PARADO;
  }

  // 2) Medicion de RPM (siempre) + PID (solo en marcha) cada DT_MS
  unsigned long dtMs = ahora - tPID;
  if (dtMs < DT_MS) return;
  tPID = ahora;

  noInterrupts();
  unsigned long c  = cuentaTaco;
  unsigned long t1 = tPrimero;
  unsigned long t2 = tUltimoPulso;
  cuentaTaco = 0;
  interrupts();

  float rpmNueva = 0;
  unsigned long span = t2 - t1;
  if (c >= 2 && span > 0) {
    rpmNueva = (c - 1) * 1000000.0 / (float)span * 60.0 / PULSOS_POR_VUELTA;
  }
  rpm = 0.5 * rpm + 0.5 * rpmNueva;

  if (motorEstado != M_MARCHA) return;

  // Seguridad: sin senal del tacometro no hay lazo
  if (c < 2) msSinTaco += dtMs; else msSinTaco = 0;
  if (msSinTaco >= T_SIN_TACO_MS) {
    falloTacometro = true;
    motorDetener();
    return;
  }

  float dt = dtMs / 1000.0;
  if (setpoint < setpointFinal) {
    setpoint = min(setpoint + RAMPA_RPM_S * dt, setpointFinal);
  }
  float error = setpoint - rpm;
  integral += Ki * error * dt;
  integral = constrain(integral, 0.0, POT_MAX_PID);
  float salida = Kp * error + integral;
  aplicarPotencia(constrain(salida, 0.0, POT_MAX_PID));
}

// =====================================================================

void apagarTodo() {
  motorDetener();                       // TRIAC off ya; reles del motor se sueltan solos
  setRelay(PIN_LLENADO, false);
  setRelay(PIN_DRENAJE, false);
  setRelay(PIN_SUAVIZANTE, false);
  setRelay(PIN_CENTRIFUGA, false);
  noTone(PIN_BUZZER); // Apaga el tono de frecuencia por seguridad
}

void mostrarLCD(String linea1, String linea2) {
  String textSerie1 = linea1;
  String textSerie2 = linea2;

  while (linea1.length() < 16) linea1 += " ";
  while (linea2.length() < 16) linea2 += " ";

  bool huboCambio = false;

  if (linea1 != ultimaLinea1) {
    lcd.setCursor(0, 0);
    lcd.print(linea1);
    ultimaLinea1 = linea1;
    huboCambio = true;
  }
  if (linea2 != ultimaLinea2) {
    lcd.setCursor(0, 1);
    lcd.print(linea2);
    ultimaLinea2 = linea2;
    huboCambio = true;
  }

  if (huboCambio) {
#ifdef DEBUG_LCD_SERIAL
    Serial.print("[LCD] L1: ");
    Serial.print(textSerie1);
    Serial.print(" | L2: ");
    Serial.println(textSerie2);
#endif
  }
}

bool presostatoLleno() {
  return digitalRead(PIN_PRESOSTATO) == LOW;
}

bool leerStartPresionado() {
  static bool ultimaLecturaRaw = HIGH;
  bool lecturaActual = digitalRead(PIN_START);
  bool presionado = false;

  if (lecturaActual != ultimaLecturaRaw) {
    tUltimoCambioBoton = millis();
  }

  if ((millis() - tUltimoCambioBoton) > DEBOUNCE_MS) {
    if (lecturaActual == LOW && estadoBotonAnterior == HIGH) {
      presionado = true;
    }
    estadoBotonAnterior = lecturaActual;
    pulsadorActivo = (estadoBotonAnterior == LOW);  // se reporta al dashboard
  }

  ultimaLecturaRaw = lecturaActual;
  return presionado;
}

void cambiarFase(Fase nuevaFase) {
  faseActual = nuevaFase;
  tInicioFase = millis();
  apagarTodo();

  if (nuevaFase == LAVADO || nuevaFase == ENJUAGUE || nuevaFase == ENJUAGUE_SUAVIZANTE) {
    girosContados = 0;
    motorSentidoCW = true;
    motorActivo = false;
    tUltimoGiro = millis();
  }
}

void manejarLlenado(Fase siguienteFase) {
  setRelay(PIN_LLENADO, true);
  if (presostatoLleno()) {
    cambiarFase(siguienteFase);
  }
}

void manejarDrenaje(Fase siguienteFase) {
  setRelay(PIN_DRENAJE, true);
  if (millis() - tInicioFase >= T_DRENAJE) {
    cambiarFase(siguienteFase);
  }
}

void manejarAgitadoPorCiclos(Fase siguienteFase) {
  if (girosContados >= MAX_GIROS) {
    cambiarFase(siguienteFase);
    return;
  }

  unsigned long enCiclo = millis() - tUltimoGiro;

  if (motorActivo) {
    if (enCiclo >= T_GIRO) {
      motorDetener();
      tUltimoGiro = millis();

      if (motorSentidoCW == false) {
        girosContados++;
      }

      motorSentidoCW = !motorSentidoCW;
    }
  } else {
    if (enCiclo >= T_PAUSA_GIRO) {
      if (motorIniciar(motorSentidoCW, RPM_AGITADO)) {
        tUltimoGiro = millis();
      }
    }
  }
}

// ================= REPORTE DE ESTADO (JSON) =================

const char* nombreFase(Fase f) {
  switch (f) {
    case REPOSO:          return "REPOSO";
    case LLENADO:         return "LLENADO";
    case LAVADO:          return "LAVADO";
    case DRENAJE_1:       return "DRENAJE_1";
    case LLENADO_ENJUAGUE:return "LLENADO_ENJUAGUE";
    case ENJUAGUE:        return "ENJUAGUE";
    case DRENAJE_2:       return "DRENAJE_2";
    case LLENADO_SUAVIZANTE:  return "LLENADO_SUAVIZANTE";
    case ENJUAGUE_SUAVIZANTE: return "ENJUAGUE_SUAVIZANTE";
    case DRENAJE_3:       return "DRENAJE_3";
    case CENTRIFUGADO:    return "CENTRIFUGADO";
    case FIN:             return "FIN";
    case ERROR_AGUA:      return "ERROR_AGUA";
    case ERROR_MOTOR:     return "ERROR_MOTOR";
  }
  return "DESCONOCIDO";
}

const char* estadoCiclo(Fase f) {
  switch (f) {
    case REPOSO:          return "inactivo";
    case LLENADO:
    case LLENADO_ENJUAGUE:return "llenando";
    case LAVADO:          return "lavando";
    case LLENADO_SUAVIZANTE:  return "llenando";
    case ENJUAGUE:
    case ENJUAGUE_SUAVIZANTE: return "enjuagando";
    case DRENAJE_1:
    case DRENAJE_2:
    case DRENAJE_3:       return "desagotando";
    case CENTRIFUGADO:    return "centrifugando";
    case FIN:             return "finalizado";
    case ERROR_AGUA:
    case ERROR_MOTOR:     return "error";
  }
  return "desconocido";
}

bool releEncendido(uint8_t pin) {
  return digitalRead(pin) == (RELAY_ACTIVE_LOW ? LOW : HIGH);
}

void enviarEstadoJSON() {
  // ArduinoJson v7: JsonDocument se dimensiona solo (heap) y se libera al
  // salir de la función.
  JsonDocument doc;

  bool enError = (faseActual == ERROR_AGUA || faseActual == ERROR_MOTOR);

  doc["estado_ciclo"] = estadoCiclo(faseActual);
  doc["fase"]        = nombreFase(faseActual);
  if (faseActual == ERROR_AGUA) {
    doc["error"] = "FALTA DE AGUA";
  } else if (faseActual == ERROR_MOTOR) {
    doc["error"] = "FALLA TACOMETRO";
  } else {
    doc["error"] = (const char*)nullptr;
  }

  if (faseActual == LAVADO || faseActual == ENJUAGUE || faseActual == ENJUAGUE_SUAVIZANTE) {
    doc["giros"]     = girosContados;
    doc["max_giros"] = MAX_GIROS;
  } else {
    doc["giros"]     = (const char*)nullptr;
    doc["max_giros"] = (const char*)nullptr;
  }

  // Texto que se está mostrando en el LCD (con relleno; el dashboard recorta)
  JsonArray lcd = doc.createNestedArray("lcd");
  lcd.add(ultimaLinea1);
  lcd.add(ultimaLinea2);

  JsonObject componentes = doc.createNestedObject("componentes");
  componentes["bomba_llenado"]  = releEncendido(PIN_LLENADO);
  componentes["motor_sentido1"] = releEncendido(PIN_MOTOR_CW);
  componentes["motor_sentido2"] = releEncendido(PIN_MOTOR_CCW);
  componentes["bomba_desagote"] = releEncendido(PIN_DRENAJE);
  componentes["bloqueo_puerta"] = releEncendido(PIN_TRABA);     // PIN_TRABA = "En marcha"
  componentes["suavizante"]     = releEncendido(PIN_SUAVIZANTE);
  componentes["centrifuga"]     = releEncendido(PIN_CENTRIFUGA) ||
                                  (CENTRIFUGA_POR_MOTOR && faseActual == CENTRIFUGADO && motorEstado == M_MARCHA);
  componentes["buzzer"]         = enError && alternarTextoError;

  // Telemetria del motor (el dashboard puede ignorarla)
  JsonObject motor = doc.createNestedObject("motor");
  motor["rpm"]      = (int)rpm;
  motor["objetivo"] = (int)setpoint;
  motor["potencia"] = (int)potencia;

  doc["presostato"] = presostatoLleno();
  doc["pulsador"]   = pulsadorActivo;

  serializeJson(doc, Serial);
  Serial.println();
}

void actualizarEnvioEstado() {
  if (millis() - tUltimoEnvioEstado >= INTERVALO_ENVIO_MS) {
    tUltimoEnvioEstado = millis();
    enviarEstadoJSON();
  }
}

// ================= SETUP =================

void setup() {
  Serial.begin(9600);

  // TRIAC: salida apagada y Timer1 en modo normal.
  // (init() de Arduino deja Timer1 en PWM; hay que pasarlo a modo normal.)
  pinMode(PIN_MOC, OUTPUT);
  digitalWrite(PIN_MOC, LOW);
  TCCR1A = 0;
  TCCR1B = 0;
  TIMSK1 = 0;

  pinMode(PIN_START, INPUT_PULLUP);
  pinMode(PIN_PRESOSTATO, INPUT_PULLUP);
  pinMode(PIN_ZC, INPUT_PULLUP);
  pinMode(PIN_TACO, INPUT_PULLUP);

  pinMode(PIN_TRABA, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LLENADO, OUTPUT);
  pinMode(PIN_MOTOR_CW, OUTPUT);
  pinMode(PIN_MOTOR_CCW, OUTPUT);
  pinMode(PIN_DRENAJE, OUTPUT);
  pinMode(PIN_SUAVIZANTE, OUTPUT);
  pinMode(PIN_CENTRIFUGA, OUTPUT);

  apagarTodo();
  setRelay(PIN_MOTOR_CW, false);
  setRelay(PIN_MOTOR_CCW, false);
  setRelay(PIN_TRABA, false);

  tLatido = millis();
  attachInterrupt(digitalPinToInterrupt(PIN_ZC), cruceCero, RISING);

  attachInterrupt(digitalPinToInterrupt(PIN_TACO), pulsoTaco, RISING);

  lcd.begin(16, 2);
  mostrarLCD("   LAVARROPAS   ", "Presione START  ");

  tPID = millis();
}

// ================= LOOP =================

void loop() {
  // Latido: mientras loop() corre, el TRIAC puede disparar
  noInterrupts();
  tLatido = millis();
  interrupts();

  actualizarMotor();

  bool startPresionado = leerStartPresionado();

  // Falla de tacometro: pasa a ERROR_MOTOR (el motor ya fue detenido)
  if (falloTacometro) {
    falloTacometro = false;
    if (faseActual != ERROR_AGUA && faseActual != ERROR_MOTOR) {
      fasePreviaAlError = faseActual;
      apagarTodo();
      faseActual = ERROR_MOTOR;
      tErrorMensaje = millis();
    }
  }

  if ((faseActual == LAVADO || faseActual == ENJUAGUE || faseActual == ENJUAGUE_SUAVIZANTE) && !presostatoLleno()) {
    fasePreviaAlError = faseActual;
    apagarTodo();
    faseActual = ERROR_AGUA;
    tErrorMensaje = millis();
  }

  if (faseActual == REPOSO) {
    mostrarLCD("   LAVARROPAS   ", "Presione START  ");
    if (startPresionado) {
      setRelay(PIN_TRABA, true);
      cambiarFase(LLENADO);
    }
    actualizarEnvioEstado();  // reporta también mientras está en reposo
    return;
  }

  switch (faseActual) {

    case LLENADO:
      mostrarLCD("CARGANDO AGUA   ", "Lavado principal");
      manejarLlenado(LAVADO);
      break;

    case LAVADO:
      if (motorActivo) {
        String msgCiclo = "Giro " + String(girosContados + 1) + "/3 " + (motorSentidoCW ? ">>" : "<<");
        mostrarLCD("FASE: LAVADO    ", msgCiclo);
      } else {
        mostrarLCD("FASE: LAVADO    ", "Pausa de vaiven ");
      }
      manejarAgitadoPorCiclos(DRENAJE_1);
      break;

    case DRENAJE_1:
      mostrarLCD("DESAGOTANDO...  ", "Fin de lavado   ");
      manejarDrenaje(LLENADO_ENJUAGUE);
      break;

    case LLENADO_ENJUAGUE:
      mostrarLCD("CARGANDO AGUA   ", "Fase: Enjuague  ");
      manejarLlenado(ENJUAGUE);
      break;

    case ENJUAGUE:
      if (motorActivo) {
        String msgCiclo = "Giro " + String(girosContados + 1) + "/3 " + (motorSentidoCW ? ">>" : "<<");
        mostrarLCD("FASE: ENJUAGUE  ", msgCiclo);
      } else {
        mostrarLCD("FASE: ENJUAGUE  ", "Pausa de vaiven ");
      }
      manejarAgitadoPorCiclos(DRENAJE_2);
      break;

    case DRENAJE_2:
      mostrarLCD("DESAGOTANDO...  ", "Fin de enjuague ");
      manejarDrenaje(LLENADO_SUAVIZANTE);
      break;

    case LLENADO_SUAVIZANTE:
      // Carga agua AL MISMO TIEMPO que abre el suavizante.
      mostrarLCD("CARGANDO AGUA   ", "Enjuague+suav.  ");
      setRelay(PIN_SUAVIZANTE, true);
      manejarLlenado(ENJUAGUE_SUAVIZANTE);
      break;

    case ENJUAGUE_SUAVIZANTE:
      // Mueve la ropa (mismo vaiven) para mezclar el suavizante.
      if (motorActivo) {
        String msgCiclo = "Giro " + String(girosContados + 1) + "/3 " + (motorSentidoCW ? ">>" : "<<");
        mostrarLCD("ENJUAGUE FINAL  ", msgCiclo);
      } else {
        mostrarLCD("ENJUAGUE FINAL  ", "Pausa de vaiven ");
      }
      manejarAgitadoPorCiclos(DRENAJE_3);
      break;

    case DRENAJE_3:
      mostrarLCD("DESAGOTANDO...  ", "Suavizante      ");
      manejarDrenaje(CENTRIFUGADO);
      break;

    case CENTRIFUGADO:
      mostrarLCD("CENTRIFUGANDO   ", "Alta velocidad  ");
      if (CENTRIFUGA_POR_MOTOR) {
        // El propio motor centrifuga: se arranca una vez (con rampa) y
        // actualizarMotor() lo mantiene.
        if (motorEstado == M_PARADO) {
          motorIniciar(SENTIDO_CENTRIFUGADO_CW, RPM_CENTRIFUGADO);
        }
      } else {
        setRelay(PIN_CENTRIFUGA, true);   // como el original
      }
      if (millis() - tInicioFase >= T_CENTRIFUGADO) {
        cambiarFase(FIN);
      }
      break;

    case FIN: {
      // La traba se suelta solo cuando el motor esta parado Y el tacometro
      // indica que el tambor ya no gira (despues de centrifugar tarda en frenar).
      bool tamborParado = (motorEstado == M_PARADO) && (rpm < RPM_PUERTA_SEGURA);
      if (tamborParado) {
        setRelay(PIN_TRABA, false);
        mostrarLCD("CICLO TERMINADO ", "Retire la ropa  ");
      } else {
        mostrarLCD("CICLO TERMINADO ", "Esperando parada");
      }

      if (startPresionado && tamborParado) {
        lcd.clear();
        ultimaLinea1 = "";
        ultimaLinea2 = "";
        cambiarFase(REPOSO);
      }
      break;
    }

    case ERROR_AGUA:
    case ERROR_MOTOR:
      if (millis() - tErrorMensaje >= 500) {
        alternarTextoError = !alternarTextoError;
        tErrorMensaje = millis();

        if (alternarTextoError) {
          tone(PIN_BUZZER, 2500); // 2500 Hz (sonido fuerte)
        } else {
          noTone(PIN_BUZZER);
        }
      }

      if (alternarTextoError) {
        if (faseActual == ERROR_AGUA) {
          mostrarLCD("  ! ALARMA !    ", " FALTA DE AGUA  ");
        } else {
          mostrarLCD("  ! ALARMA !    ", " FALLA TACOMETRO");
        }
      } else {
        mostrarLCD("                ", "Pulse START-RST ");
      }

      if (startPresionado) {
        // Agua: solo reanuda si el tanque lleno. Motor: reanuda y, si el
        // tacometro sigue sin senal, vuelve a dar error a los 2 s.
        if (faseActual == ERROR_MOTOR || presostatoLleno()) {
          noTone(PIN_BUZZER);
          Fase destino = fasePreviaAlError;
          if (destino == CENTRIFUGADO) tInicioFase = millis();  // repite el centrifugado completo
          faseActual = destino;
          tUltimoGiro = millis();
          Serial.println("Error solucionado. Reanudando ciclo...");
        } else {
          Serial.println("Intento de reanudar fallido: El tanque sigue vacío.");
        }
      }
      break;

    default:
      cambiarFase(REPOSO);
      break;
  }

  actualizarEnvioEstado();
}
