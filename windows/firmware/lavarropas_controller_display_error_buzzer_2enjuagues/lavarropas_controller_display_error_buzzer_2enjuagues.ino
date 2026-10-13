/*
  CONTROLADOR DE LAVARROPAS - Arduino UNO
  Maquina de estados no bloqueante (millis(), sin delay())
  Display LCD 16x2 en modo paralelo (4 bits), Monitor Serie y Alarma Sonora Potente

  VARIANTE: 2 ENJUAGUES (prototipo)
  ----------------------------------------------------------------
  Copia derivada del firmware original del profesor. El original NO se toca.
  Unica diferencia respecto de "lavarropas_controller_display_error_buzzer.ino":
  la fase SUAVIZANTE "seca" se reemplaza por dos fases que imitan un enjuague
  final real:

    DRENAJE_2 -> LLENADO_SUAVIZANTE -> ENJUAGUE_SUAVIZANTE -> DRENAJE_3

    - LLENADO_SUAVIZANTE : carga agua AL MISMO TIEMPO que abre el suavizante.
    - ENJUAGUE_SUAVIZANTE: mueve la ropa (mismo vaiven) para mezclar.

  Todo lo demas (pines, tiempos, LCD, buzzer, reporte JSON) es identico al
  original adaptado. Ver integracion/DECISIONES.md seccion 8.
*/

#include <LiquidCrystal.h>
#include <ArduinoJson.h>   // agregado: reporte de estado en JSON para el dashboard

// #define DEBUG_LCD_SERIAL   // Descomentar para volcar el texto del LCD por Serial.
//                             // Por defecto queda APAGADO: el estado va por JSON.

// ---------- LCD ----------
LiquidCrystal lcd(A2, 6, A3, 5, A0, A1);

// ---------- Pines de entrada ----------
const uint8_t PIN_START      = 2;
const uint8_t PIN_PRESOSTATO = 3;

// ---------- Pines de salida (relés y alarmas) ----------
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
  LLENADO_SUAVIZANTE,   // variante: carga agua + suavizante
  ENJUAGUE_SUAVIZANTE,  // variante: mueve la ropa para mezclar el suavizante
  DRENAJE_3,
  CENTRIFUGADO,
  FIN,
  ERROR_AGUA 
};

Fase faseActual = REPOSO;
Fase fasePreviaAlError = REPOSO; 

// ---------- Temporizados de fases fijas ----------
const unsigned long T_DRENAJE       = 10000UL; 
// T_SUAVIZANTE: en la variante 2 enjuagues la fase de suavizante ya no se
// temporiza (se llena y se agita). Se conserva el valor por referencia.
// const unsigned long T_SUAVIZANTE = 10000UL;
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
bool motorActivo = false;
bool alternarTextoError = true;

// ---------- Reporte de estado al dashboard (agregado) ----------
unsigned long tUltimoEnvioEstado = 0;
const unsigned long INTERVALO_ENVIO_MS = 1000;  // 1 línea JSON por segundo
bool pulsadorActivo = false;                     // estado ya antirrebotado del START

// ---------- Antirrebote del botón Start ----------
unsigned long tUltimoCambioBoton = 0;
const unsigned long DEBOUNCE_MS = 50;
bool estadoBotonAnterior = HIGH;

// Variables globales para el LCD y Monitor Serie
String ultimaLinea1 = "";
String ultimaLinea2 = "";

// ================= FUNCIONES AUXILIARES =================

void setRelay(uint8_t pin, bool encender) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(pin, encender ? LOW : HIGH);
  } else {
    digitalWrite(pin, encender ? HIGH : LOW);
  }
}

void apagarTodo() {
  setRelay(PIN_LLENADO, false);
  setRelay(PIN_MOTOR_CW, false);
  setRelay(PIN_MOTOR_CCW, false);
  setRelay(PIN_DRENAJE, false);
  setRelay(PIN_SUAVIZANTE, false);
  setRelay(PIN_CENTRIFUGA, false);
  noTone(PIN_BUZZER); // Apaga el tono de frecuencia por seguridad
  motorActivo = false;
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
      setRelay(PIN_MOTOR_CW, false);
      setRelay(PIN_MOTOR_CCW, false);
      motorActivo = false;
      tUltimoGiro = millis(); 
      
      if (motorSentidoCW == false) {
        girosContados++; 
      }
      
      motorSentidoCW = !motorSentidoCW;
    }
  } else {
    if (enCiclo >= T_PAUSA_GIRO) {
      if (motorSentidoCW) {
        setRelay(PIN_MOTOR_CW, true);
        setRelay(PIN_MOTOR_CCW, false);
      } else {
        setRelay(PIN_MOTOR_CW, false);
        setRelay(PIN_MOTOR_CCW, true);
      }
      motorActivo = true;
      tUltimoGiro = millis(); 
    }
  }
}

// ================= REPORTE DE ESTADO (agregado) =================
//
// Traduce la fase interna a un nombre estable para el dashboard y arma
// una línea JSON, igual que la que ya espera main.py.

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
    case ERROR_AGUA:      return "error";
  }
  return "desconocido";
}

bool releEncendido(uint8_t pin) {
  return digitalRead(pin) == (RELAY_ACTIVE_LOW ? LOW : HIGH);
}

void enviarEstadoJSON() {
  // ArduinoJson v7: JsonDocument se dimensiona solo (heap) y se libera al
  // salir de la función. Evita reservar cientos de bytes en el stack del Uno.
  JsonDocument doc;

  doc["estado_ciclo"] = estadoCiclo(faseActual);
  doc["fase"]        = nombreFase(faseActual);
  if (faseActual == ERROR_AGUA) {
    doc["error"] = "FALTA DE AGUA";
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
  componentes["centrifuga"]     = releEncendido(PIN_CENTRIFUGA);
  componentes["buzzer"]         = (faseActual == ERROR_AGUA) && alternarTextoError;

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

// ================= COMANDOS POR SERIAL (Opción 3.B, DESACTIVADA) =================
//
// La Opción 3.A deja el dashboard en modo SOLO LECTURA: no se leen comandos.
// Si más adelante se quiere poder arrancar / resetear desde el dashboard,
// descomentar el bloque de abajo y llamar a leerComandosSerial() en loop().
// El backend y el dashboard ya tienen preparado el camino (ver README).
//
// String bufferSerial = "";
//
// void procesarComando(String linea) {
//   linea.trim();
//   if (linea == "CMD_START") {
//     // Equivale a una pulsación del botón físico: solo se atiende en REPOSO.
//     if (faseActual == REPOSO) {
//       setRelay(PIN_TRABA, true);
//       cambiarFase(LLENADO);
//     } else if (faseActual == ERROR_AGUA && presostatoLleno()) {
//       noTone(PIN_BUZZER);
//       faseActual = fasePreviaAlError;
//       tUltimoGiro = millis();
//     } else if (faseActual == FIN) {
//       lcd.clear();
//       ultimaLinea1 = "";
//       ultimaLinea2 = "";
//       cambiarFase(REPOSO);
//     }
//   }
// }
//
// void leerComandosSerial() {
//   while (Serial.available()) {
//     char c = Serial.read();
//     if (c == '\n') {
//       procesarComando(bufferSerial);
//       bufferSerial = "";
//     } else if (c != '\r') {
//       bufferSerial += c;
//     }
//   }
// }

// ================= SETUP =================

void setup() {
  Serial.begin(9600);

  pinMode(PIN_START, INPUT_PULLUP);
  pinMode(PIN_PRESOSTATO, INPUT_PULLUP);

  pinMode(PIN_TRABA, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT); 
  pinMode(PIN_LLENADO, OUTPUT);
  pinMode(PIN_MOTOR_CW, OUTPUT);
  pinMode(PIN_MOTOR_CCW, OUTPUT);
  pinMode(PIN_DRENAJE, OUTPUT);
  pinMode(PIN_SUAVIZANTE, OUTPUT);
  pinMode(PIN_CENTRIFUGA, OUTPUT);

  apagarTodo();
  setRelay(PIN_TRABA, false);

  lcd.begin(16, 2);
  mostrarLCD("   LAVARROPAS   ", "Presione START  ");
}

// ================= LOOP =================

void loop() {
  bool startPresionado = leerStartPresionado();

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
      // Variante: carga agua AL MISMO TIEMPO que abre el suavizante.
      mostrarLCD("CARGANDO AGUA   ", "Enjuague+suav.  ");
      setRelay(PIN_SUAVIZANTE, true);
      manejarLlenado(ENJUAGUE_SUAVIZANTE);
      break;

    case ENJUAGUE_SUAVIZANTE:
      // Variante: mueve la ropa (mismo vaiven) para mezclar el suavizante.
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
      setRelay(PIN_CENTRIFUGA, true);
      if (millis() - tInicioFase >= T_CENTRIFUGADO) {
        cambiarFase(FIN);
      }
      break;

    case FIN: 
      setRelay(PIN_TRABA, false); 
      mostrarLCD("CICLO TERMINADO ", "Retire la ropa  ");
      
      if (startPresionado) {
        lcd.clear();
        ultimaLinea1 = ""; 
        ultimaLinea2 = "";
        cambiarFase(REPOSO);
      }
      break;

    case ERROR_AGUA:
      if (millis() - tErrorMensaje >= 500) { // Bajé el tiempo a 500ms para una alarma más ágil
        alternarTextoError = !alternarTextoError;
        tErrorMensaje = millis();
        
        // CORREGIDO: Usamos tone() para generar un pitido real oscilando las bobinas del buzzer
        if (alternarTextoError) {
          tone(PIN_BUZZER, 2500); // Envía una frecuencia de 2500 Hz (Sonido fuerte)
        } else {
          noTone(PIN_BUZZER);     // Silencia el pin por completo
        }
      }

      if (alternarTextoError) {
        mostrarLCD("  ! ALARMA !    ", " FALTA DE AGUA  ");
      } else {
        mostrarLCD("                ", "Pulse START-RST ");
      }

      if (startPresionado) {
        if (presostatoLleno()) {
          noTone(PIN_BUZZER); // Se apaga la frecuencia al salir del error
          faseActual = fasePreviaAlError;
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
