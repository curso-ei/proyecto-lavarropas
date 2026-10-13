"""
Simulador de Arduino para probar el dashboard sin hardware real.

Imita al firmware del profesor `lavarropas_controller_display_error_buzzer.ino`:
emite por Serial el mismo JSON que el dashboard espera, con las fases,
componentes, LCD, giros y alarma reales del ciclo.

Uso:
  1) Crear el par de puertos virtuales (dejar corriendo en su propia terminal):
       socat -d -d pty,raw,echo=0 pty,raw,echo=0

     socat va a imprimir dos rutas, por ejemplo:
       PTY is /dev/pts/3
       PTY is /dev/pts/4

  2) Correr este simulador apuntando a UNO de los dos puertos:
       python simulador.py /dev/pts/4

  3) Correr main.py (el backend real) apuntando al OTRO puerto:
       PUERTO_FORZADO=/dev/pts/3 uvicorn main:app --host 0.0.0.0 --port 8000

El simulador reproduce la lógica del firmware:

  - El presostato manda:
      * Arrancar (opción 1 desde REPOSO) entra a LLENADO aunque no haya nivel.
      * LLENADO / LLENADO_ENJUAGUE no avanzan sin presostato (avisa por consola).
      * LAVADO / ENJUAGUE pasan a ERROR_AGUA si el presostato cae.
      * Al terminar un DRENAJE el presostato se apaga solo (tambor vacío).
  - Las fases con temporizador (DRENAJE 10 s, SUAVIZANTE 10 s, CENTRIFUGADO
    15 s) avanzan SOLAS, igual que el firmware. LLENADO avanza solo cuando el
    presostato marca nivel.
  - El vaivén del motor (LAVADO / ENJUAGUE) es AUTOMÁTICO y por tiempo: 5 s
    girando, 2 s de pausa, alternando sentido, hasta completar 3 giros.
  - La opción 7 es un "avance libre" que ignora condiciones, solo para mirar
    pantallas. Es una copia aparte: no afecta al Arduino real.
  - La opción 8 cambia la velocidad de simulación (1x, 2x, 5x, 10x) para no
    tener que esperar los tiempos reales del firmware.
  - Acepta CMD_START / CMD_RESET (Opción 3.B, arranque remoto). El dashboard
    viene configurado en solo lectura, así que normalmente no llegan.
"""

import json
import os
import sys
import threading
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/pts/4"
BAUD = 9600

# Variante de ciclo a simular (ver sección "Variante de ciclo" más abajo):
#   "original"      -> el firmware del profesor, tal cual.
#   "dos_enjuagues" -> prototipo con un segundo enjuague (el final) que carga
#                      agua + suavizante y mueve la ropa.
# Se elige con la variable de entorno CICLO, ej:
#   CICLO=dos_enjuagues python simulador.py /dev/pts/4
VARIANTE = os.environ.get("CICLO", "original").strip().lower()
if VARIANTE != "dos_enjuagues":
    VARIANTE = "original"

# Fases reales del firmware del profesor.
FASES = [
    "REPOSO",
    "LLENADO",
    "LAVADO",
    "DRENAJE_1",
    "LLENADO_ENJUAGUE",
    "ENJUAGUE",
    "DRENAJE_2",
    "SUAVIZANTE",
    "DRENAJE_3",
    "CENTRIFUGADO",
    "FIN",
    "ERROR_AGUA",
]

# Traducción a estado_ciclo que muestra el dashboard.
ESTADO_CICLO = {
    "REPOSO": "inactivo",
    "LLENADO": "llenando",
    "LAVADO": "lavando",
    "DRENAJE_1": "desagotando",
    "LLENADO_ENJUAGUE": "llenando",
    "ENJUAGUE": "enjuagando",
    "DRENAJE_2": "desagotando",
    "SUAVIZANTE": "suavizando",
    "DRENAJE_3": "desagotando",
    "CENTRIFUGADO": "centrifugando",
    "FIN": "finalizado",
    "ERROR_AGUA": "error",
}

# Qué componentes están encendidos al ENTRAR a cada fase, según el firmware.
# PIN_TRABA ("bloqueo_puerta") queda activo durante todo el ciclo, salvo en
# REPOSO/FIN. El motor de LAVADO/ENJUAGUE lo maneja el vaivén automático, por
# eso figura apagado al entrar (el firmware arranca en pausa). El buzzer solo
# suena en ERROR_AGUA (se agrega aparte).
FASE_COMPONENTES = {
    "REPOSO": {},
    "LLENADO": {"bloqueo_puerta": True, "bomba_llenado": True},
    "LAVADO": {"bloqueo_puerta": True},
    "DRENAJE_1": {"bloqueo_puerta": True, "bomba_desagote": True},
    "LLENADO_ENJUAGUE": {"bloqueo_puerta": True, "bomba_llenado": True},
    "ENJUAGUE": {"bloqueo_puerta": True},
    "DRENAJE_2": {"bloqueo_puerta": True, "bomba_desagote": True},
    "SUAVIZANTE": {"bloqueo_puerta": True, "suavizante": True},
    "DRENAJE_3": {"bloqueo_puerta": True, "bomba_desagote": True},
    "CENTRIFUGADO": {"bloqueo_puerta": True, "centrifuga": True},
    "FIN": {},
    "ERROR_AGUA": {},
}

# Texto del LCD 16x2 por fase (línea 1, línea 2), igual que en el firmware.
FASE_LCD = {
    "REPOSO": ["   LAVARROPAS   ", "Presione START  "],
    "LLENADO": ["CARGANDO AGUA   ", "Lavado principal"],
    "LAVADO": ["FASE: LAVADO    ", "Pausa de vaiven "],
    "DRENAJE_1": ["DESAGOTANDO...  ", "Fin de lavado   "],
    "LLENADO_ENJUAGUE": ["CARGANDO AGUA   ", "Fase: Enjuague  "],
    "ENJUAGUE": ["FASE: ENJUAGUE  ", "Pausa de vaiven "],
    "DRENAJE_2": ["DESAGOTANDO...  ", "Fin de enjuague "],
    "SUAVIZANTE": ["AGREGANDO...    ", "Suavizante      "],
    "DRENAJE_3": ["DESAGOTANDO...  ", "Suavizante      "],
    "CENTRIFUGADO": ["CENTRIFUGANDO   ", "Alta velocidad  "],
    "FIN": ["CICLO TERMINADO ", "Retire la ropa  "],
    "ERROR_AGUA": ["  ! ALARMA !    ", " FALTA DE AGUA  "],
}

# Tiempos del firmware (ms -> segundos).
MAX_GIROS = 3
T_GIRO = 5.0           # duración de cada giro
T_PAUSA_GIRO = 2.0     # pausa entre giros
T_DRENAJE = 10.0       # duración de cada drenaje
T_SUAVIZANTE = 10.0    # duración de la fase de suavizante
T_CENTRIFUGADO = 15.0  # duración del centrifugado

# Factor de velocidad de la simulación (1 = tiempos reales del firmware).
# Se cambia con la opción 8 del menú. NO afecta al Arduino real.
factor_velocidad = 1

# Transiciones nominales del ciclo (lo que ocurre cuando se cumplen las
# condiciones de cada fase). ERROR_AGUA no está acá: se sale reseteando.
FASE_SIGUIENTE = {
    "REPOSO": "LLENADO",
    "LLENADO": "LAVADO",
    "LAVADO": "DRENAJE_1",
    "DRENAJE_1": "LLENADO_ENJUAGUE",
    "LLENADO_ENJUAGUE": "ENJUAGUE",
    "ENJUAGUE": "DRENAJE_2",
    "DRENAJE_2": "SUAVIZANTE",
    "SUAVIZANTE": "DRENAJE_3",
    "DRENAJE_3": "CENTRIFUGADO",
    "CENTRIFUGADO": "FIN",
    "FIN": "REPOSO",
}

FASES_LLENADO = ("LLENADO", "LLENADO_ENJUAGUE")
FASES_LAVADO_ENJUAGUE = ("LAVADO", "ENJUAGUE")
FASES_DRENAJE = ("DRENAJE_1", "DRENAJE_2", "DRENAJE_3")

# ---------------------------------------------------------------------------
# Variante de ciclo (PROTOTIPO, NO es el firmware real del profesor)
# ---------------------------------------------------------------------------
#   CICLO=original       (por defecto) -> el firmware del profesor, tal cual.
#   CICLO=dos_enjuagues  -> propuesta: un segundo enjuague (el final) que
#                           carga agua + suavizante y mueve la ropa, en vez
#                           de la fase SUAVIZANTE "seca" del original.
#
# Vive SOLO en el simulador. No cambia el .ino del profesor. Sirve para ver
# cómo se comportaría el dashboard y comparar contra el ciclo original.
if VARIANTE == "dos_enjuagues":
    FASES = [
        "REPOSO",
        "LLENADO",
        "LAVADO",
        "DRENAJE_1",
        "LLENADO_ENJUAGUE",
        "ENJUAGUE",
        "DRENAJE_2",
        "LLENADO_SUAVIZANTE",
        "ENJUAGUE_SUAVIZANTE",
        "DRENAJE_3",
        "CENTRIFUGADO",
        "FIN",
        "ERROR_AGUA",
    ]
    ESTADO_CICLO = {
        "REPOSO": "inactivo",
        "LLENADO": "llenando",
        "LAVADO": "lavando",
        "DRENAJE_1": "desagotando",
        "LLENADO_ENJUAGUE": "llenando",
        "ENJUAGUE": "enjuagando",
        "DRENAJE_2": "desagotando",
        "LLENADO_SUAVIZANTE": "llenando",
        "ENJUAGUE_SUAVIZANTE": "enjuagando",
        "DRENAJE_3": "desagotando",
        "CENTRIFUGADO": "centrifugando",
        "FIN": "finalizado",
        "ERROR_AGUA": "error",
    }
    FASE_COMPONENTES = {
        "REPOSO": {},
        "LLENADO": {"bloqueo_puerta": True, "bomba_llenado": True},
        "LAVADO": {"bloqueo_puerta": True},
        "DRENAJE_1": {"bloqueo_puerta": True, "bomba_desagote": True},
        "LLENADO_ENJUAGUE": {"bloqueo_puerta": True, "bomba_llenado": True},
        "ENJUAGUE": {"bloqueo_puerta": True},
        "DRENAJE_2": {"bloqueo_puerta": True, "bomba_desagote": True},
        "LLENADO_SUAVIZANTE": {
            "bloqueo_puerta": True,
            "bomba_llenado": True,
            "suavizante": True,
        },
        "ENJUAGUE_SUAVIZANTE": {"bloqueo_puerta": True},
        "DRENAJE_3": {"bloqueo_puerta": True, "bomba_desagote": True},
        "CENTRIFUGADO": {"bloqueo_puerta": True, "centrifuga": True},
        "FIN": {},
        "ERROR_AGUA": {},
    }
    FASE_LCD = {
        "REPOSO": ["   LAVARROPAS   ", "Presione START  "],
        "LLENADO": ["CARGANDO AGUA   ", "Lavado principal"],
        "LAVADO": ["FASE: LAVADO    ", "Pausa de vaiven "],
        "DRENAJE_1": ["DESAGOTANDO...  ", "Fin de lavado   "],
        "LLENADO_ENJUAGUE": ["CARGANDO AGUA   ", "Enjuague 1      "],
        "ENJUAGUE": ["ENJUAGUE 1      ", "Pausa de vaiven "],
        "DRENAJE_2": ["DESAGOTANDO...  ", "Fin enjuague 1  "],
        "LLENADO_SUAVIZANTE": ["CARGANDO AGUA   ", "Enjuague+suav.  "],
        "ENJUAGUE_SUAVIZANTE": ["ENJUAGUE FINAL  ", "Pausa de vaiven "],
        "DRENAJE_3": ["DESAGOTANDO...  ", "Suavizante      "],
        "CENTRIFUGADO": ["CENTRIFUGANDO   ", "Alta velocidad  "],
        "FIN": ["CICLO TERMINADO ", "Retire la ropa  "],
        "ERROR_AGUA": ["  ! ALARMA !    ", " FALTA DE AGUA  "],
    }
    FASE_SIGUIENTE = {
        "REPOSO": "LLENADO",
        "LLENADO": "LAVADO",
        "LAVADO": "DRENAJE_1",
        "DRENAJE_1": "LLENADO_ENJUAGUE",
        "LLENADO_ENJUAGUE": "ENJUAGUE",
        "ENJUAGUE": "DRENAJE_2",
        "DRENAJE_2": "LLENADO_SUAVIZANTE",
        "LLENADO_SUAVIZANTE": "ENJUAGUE_SUAVIZANTE",
        "ENJUAGUE_SUAVIZANTE": "DRENAJE_3",
        "DRENAJE_3": "CENTRIFUGADO",
        "CENTRIFUGADO": "FIN",
        "FIN": "REPOSO",
    }
    FASES_LLENADO = ("LLENADO", "LLENADO_ENJUAGUE", "LLENADO_SUAVIZANTE")
    FASES_LAVADO_ENJUAGUE = ("LAVADO", "ENJUAGUE", "ENJUAGUE_SUAVIZANTE")
    FASES_DRENAJE = ("DRENAJE_1", "DRENAJE_2", "DRENAJE_3")

# Título del LCD durante la agitación, por fase (sirve para ambas variantes).
TITULO_AGITACION = {
    "LAVADO": "FASE: LAVADO    ",
    "ENJUAGUE": "FASE: ENJUAGUE  ",
    "ENJUAGUE_SUAVIZANTE": "ENJUAGUE FINAL  ",
}

estado = {
    "estado_ciclo": "inactivo",
    "fase": "REPOSO",
    "error": None,
    "giros": None,
    "max_giros": None,
    "lcd": ["   LAVARROPAS   ", "Presione START  "],
    "componentes": {
        "bomba_llenado": False,
        "motor_sentido1": False,
        "motor_sentido2": False,
        "bomba_desagote": False,
        "bloqueo_puerta": False,
        "suavizante": False,
        "centrifuga": False,
        "buzzer": False,
    },
    "presostato": False,
    "pulsador": False,
}

lock = threading.Lock()

# --- Sub-estado de agitación (réplica del vaivén del firmware) ---
motor_activo = False
motor_sentido_cw = True
t_ultimo_giro = 0.0
t_inicio_fase = 0.0
fase_previa_error = "REPOSO"

try:
    ser = serial.Serial(PORT, BAUD, timeout=0.5)
except serial.SerialException as e:
    print(f"No se pudo abrir {PORT}: {e}")
    sys.exit(1)

print(f"[simulador] Escuchando en {PORT}")
print(f"[simulador] Variante de ciclo: {VARIANTE}")
if VARIANTE == "dos_enjuagues":
    print("[simulador] (prototipo: enjuague final con agua + suavizante)")


def actualizar_lcd_agitacion(fase: str):
    """Refleja en el LCD si el motor gira (y en qué sentido) o está en pausa."""
    titulo = TITULO_AGITACION.get(fase, "FASE: ENJUAGUE  ")
    if motor_activo:
        sentido = ">>" if motor_sentido_cw else "<<"
        estado["lcd"] = [titulo, f"Giro {estado['giros'] + 1}/{MAX_GIROS} {sentido}"]
    else:
        estado["lcd"] = [titulo, "Pausa de vaiven "]


def aplicar_fase(nueva_fase: str, giros: int = 0):
    """Apaga todo y prende solo lo que corresponde al entrar a la nueva fase."""
    global motor_activo, motor_sentido_cw, t_ultimo_giro, t_inicio_fase

    t_inicio_fase = time.monotonic()

    for key in estado["componentes"]:
        estado["componentes"][key] = False
    for key, valor in FASE_COMPONENTES.get(nueva_fase, {}).items():
        estado["componentes"][key] = valor

    estado["fase"] = nueva_fase
    estado["estado_ciclo"] = ESTADO_CICLO.get(nueva_fase, "desconocido")
    estado["error"] = "FALTA DE AGUA" if nueva_fase == "ERROR_AGUA" else None
    estado["lcd"] = list(FASE_LCD.get(nueva_fase, ["", ""]))

    if nueva_fase in FASES_LAVADO_ENJUAGUE:
        # El firmware entra en pausa (motor apagado) y luego empieza a girar.
        motor_activo = False
        motor_sentido_cw = True
        t_ultimo_giro = time.monotonic()
        estado["giros"] = giros
        estado["max_giros"] = MAX_GIROS
        actualizar_lcd_agitacion(nueva_fase)
    else:
        motor_activo = False
        estado["giros"] = None
        estado["max_giros"] = None


def entrar_en_error(motivo: str = "FALTA DE AGUA"):
    """Dispara ERROR_AGUA guardando la fase previa, como el firmware real."""
    global fase_previa_error
    if estado["fase"] != "ERROR_AGUA":
        fase_previa_error = estado["fase"]
    aplicar_fase("ERROR_AGUA")
    estado["componentes"]["buzzer"] = True
    print(f"[simulador] ¡{motivo}! -> ERROR_AGUA (buzzer encendido)")


def evaluar_error_por_presostato() -> bool:
    """Réplica de la línea 244 del firmware.

    Si estás en LAVADO o ENJUAGUE y el presostato deja de marcar nivel, se
    dispara ERROR_AGUA.
    """
    if estado["fase"] in FASES_LAVADO_ENJUAGUE and not estado["presostato"]:
        entrar_en_error("FALTA DE AGUA")
        return True
    return False


def actualizar_agitacion():
    """Un tick del vaivén automático (réplica de manejarAgitadoPorCiclos).

    Se llama periódicamente desde un hilo, no desde el menú. Alterna el motor
    por tiempo (T_GIRO / T_PAUSA_GIRO) y, al completar MAX_GIROS, pasa solo a
    la fase siguiente.
    """
    global motor_activo, motor_sentido_cw, t_ultimo_giro
    fase = estado["fase"]

    if fase not in FASES_LAVADO_ENJUAGUE:
        return
    if not estado["presostato"]:
        entrar_en_error("FALTA DE AGUA")
        return
    if estado["giros"] >= MAX_GIROS:
        print(f"[simulador] {MAX_GIROS} giros completados.")
        pasar_siguiente()
        return

    ahora = time.monotonic()
    if motor_activo:
        if ahora - t_ultimo_giro >= duracion(T_GIRO):
            estado["componentes"]["motor_sentido1"] = False
            estado["componentes"]["motor_sentido2"] = False
            motor_activo = False
            t_ultimo_giro = ahora
            # El firmware cuenta el giro al terminar el sentido inverso (CCW).
            if not motor_sentido_cw:
                estado["giros"] += 1
            motor_sentido_cw = not motor_sentido_cw
            actualizar_lcd_agitacion(fase)
    else:
        if ahora - t_ultimo_giro >= duracion(T_PAUSA_GIRO):
            if motor_sentido_cw:
                estado["componentes"]["motor_sentido1"] = True
                estado["componentes"]["motor_sentido2"] = False
            else:
                estado["componentes"]["motor_sentido1"] = False
                estado["componentes"]["motor_sentido2"] = True
            motor_activo = True
            t_ultimo_giro = ahora
            actualizar_lcd_agitacion(fase)


def pasar_siguiente():
    """Aplica la transición nominal de la fase actual (sin validar nada)."""
    fase = estado["fase"]
    siguiente = FASE_SIGUIENTE.get(fase)
    if siguiente is None:
        return
    giros = (estado["giros"] + 1) if estado["giros"] is not None else 0
    aplicar_fase(siguiente, giros)
    # Un drenaje completo vacía el tambor -> el presostato cae solo.
    if fase in FASES_DRENAJE:
        estado["presostato"] = False
    print(f"[simulador] Nueva fase: {siguiente} ({estado['estado_ciclo']})")


def duracion(segundos: float) -> float:
    """Ajusta un tiempo real del firmware según el factor de velocidad."""
    return segundos / factor_velocidad


def avanzar_automatico():
    """Un tick del avance por tiempo, réplica de la lógica del firmware.

    - LLENADO avanza solo cuando el presostato marca nivel.
    - DRENAJE / SUAVIZANTE / CENTRIFUGADO avanzan al cumplirse su tiempo.
    - LAVADO / ENJUAGUE hacen el vaivén por tiempo.
    """
    fase = estado["fase"]
    transcurrido = time.monotonic() - t_inicio_fase

    if fase in FASES_LAVADO_ENJUAGUE:
        actualizar_agitacion()
    elif fase in FASES_LLENADO:
        if estado["presostato"]:
            pasar_siguiente()
    elif fase in FASES_DRENAJE:
        if transcurrido >= duracion(T_DRENAJE):
            pasar_siguiente()
    elif fase == "SUAVIZANTE":
        if transcurrido >= duracion(T_SUAVIZANTE):
            pasar_siguiente()
    elif fase == "CENTRIFUGADO":
        if transcurrido >= duracion(T_CENTRIFUGADO):
            pasar_siguiente()


def avanzar_fase(respetar_condiciones: bool = True):
    """Avanza a la siguiente fase del ciclo.

    Con `respetar_condiciones=True` imita al firmware real:
      - LLENADO/LLENADO_ENJUAGUE no avanzan sin presostato.
      - LAVADO/ENJUAGUE tienen vaivén automático (no se avanzan a mano).
      - Al terminar un DRENAJE, el presostato cae (el tambor se vacía).
    Con `respetar_condiciones=False` es un avance libre, solo para mirar
    pantallas (no altera al Arduino real, es una copia aparte).
    """
    fase = estado["fase"]

    if fase == "ERROR_AGUA":
        print("[simulador] Estás en ERROR_AGUA: usá la opción 5 para resolverlo.")
        return

    if respetar_condiciones:
        # El firmware checkea el presostato durante el lavado/enjuague.
        if fase in FASES_LAVADO_ENJUAGUE and not estado["presostato"]:
            entrar_en_error("FALTA DE AGUA")
            return
        # El llenado se queda esperando a que el presostato marque nivel.
        if fase in FASES_LLENADO and not estado["presostato"]:
            print(
                f"[simulador] Esperando presostato: {fase} no avanza hasta que "
                "marque nivel (opción 2)."
            )
            return
        # El vaivén de LAVADO/ENJUAGUE avanza solo por tiempo.
        if fase in FASES_LAVADO_ENJUAGUE:
            print(
                "[simulador] La agitación avanza sola (5 s giro / 2 s pausa). "
                "Esperá a que termine, o usá la opción 7 para forzar."
            )
            return

    siguiente = FASE_SIGUIENTE.get(fase)
    if siguiente is None:
        return

    pasar_siguiente()


def reset_error():
    """Reintenta salir de ERROR_AGUA, como el botón físico del firmware."""
    if estado["fase"] != "ERROR_AGUA":
        print("[simulador] No hay error activo para resolver.")
        return
    if not estado["presostato"]:
        print("[simulador] Reset rechazado: el presostato sigue vacío.")
        return
    destino = fase_previa_error if fase_previa_error in FASES else "LAVADO"
    aplicar_fase(destino)
    print(f"[simulador] Error resuelto -> {destino}")


def procesar_comando(linea: str):
    """Interpreta CMD_START / CMD_RESET (Opción 3.B, arranque remoto).

    Simula la pulsación del botón físico, igual que el firmware cuando tiene
    habilitado su parser de comandos.
    """
    linea = linea.strip()
    if linea not in ("CMD_START", "CMD_RESET"):
        if linea:
            print(f"[simulador] Comando desconocido: {linea}")
        return

    with lock:
        fase = estado["fase"]
        if linea == "CMD_RESET" and fase == "ERROR_AGUA":
            reset_error()
        elif fase == "REPOSO":
            # Arrancar no mira el presostato (el firmware entra a LLENADO igual).
            aplicar_fase("LLENADO")
            print("[simulador] CMD_START -> LLENADO")
        elif fase == "ERROR_AGUA":
            reset_error()
        elif fase == "FIN":
            aplicar_fase("REPOSO")
            print("[simulador] CMD_START -> REPOSO")
        else:
            print(f"[simulador] CMD ignorado en fase {fase}")


def hilo_lector():
    buffer = ""
    while True:
        try:
            data = ser.read(200).decode(errors="ignore")
        except serial.SerialException:
            break
        if data:
            buffer += data
            while "\n" in buffer:
                linea, buffer = buffer.split("\n", 1)
                if linea.strip():
                    procesar_comando(linea.strip())


def hilo_escritor():
    while True:
        with lock:
            payload = json.dumps(estado)
        try:
            ser.write((payload + "\n").encode())
        except serial.SerialException:
            break
        time.sleep(1)


def hilo_tiempos():
    """Hilo que hace avanzar las fases por tiempo, como el firmware."""
    while True:
        with lock:
            if estado["fase"] not in ("REPOSO", "FIN", "ERROR_AGUA"):
                avanzar_automatico()
        time.sleep(0.2)


threading.Thread(target=hilo_lector, daemon=True).start()
threading.Thread(target=hilo_escritor, daemon=True).start()
threading.Thread(target=hilo_tiempos, daemon=True).start()

MENU = f"""
--- Simulador de Arduino (ciclo: {VARIANTE}) ---
1) Avanzar a la siguiente fase (respeta el presostato)
2) Togglear presostato
3) Togglear pulsador
4) Forzar un error de falta de agua
5) Resolver error (vuelve a la fase previa si hay nivel)
6) Ver estado actual
7) Forzar avance de fase (modo libre, ignora condiciones)
8) Cambiar velocidad de simulación
0) Salir
> """

while True:
    opcion = input(MENU).strip()
    with lock:
        if opcion == "1":
            avanzar_fase(respetar_condiciones=True)
        elif opcion == "2":
            estado["presostato"] = not estado["presostato"]
            print(f"[simulador] Presostato: {estado['presostato']}")
            evaluar_error_por_presostato()
        elif opcion == "3":
            estado["pulsador"] = not estado["pulsador"]
            print(f"[simulador] Pulsador: {estado['pulsador']}")
        elif opcion == "4":
            entrar_en_error("FALTA DE AGUA (forzado)")
        elif opcion == "5":
            reset_error()
        elif opcion == "6":
            print(json.dumps(estado, indent=2, ensure_ascii=False))
        elif opcion == "7":
            avanzar_fase(respetar_condiciones=False)
        elif opcion == "8":
            ciclo = [1, 2, 5, 10]
            idx = ciclo.index(factor_velocidad) if factor_velocidad in ciclo else 0
            factor_velocidad = ciclo[(idx + 1) % len(ciclo)]
            print(f"[simulador] Velocidad de simulación: {factor_velocidad}x")
        elif opcion == "0":
            break
        else:
            print("Opción inválida")
